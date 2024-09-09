/* TODO write some fancy header with copyright information */
/**/
/**/
/**/
/**/
/**/
/**/
/**/

#include "sdmmc_spi.h"

#include "diskio.h"

#include <string.h>

/* Definitions for MMC/SDC command */
#define CMD0     (0x40+0)     	/* GO_IDLE_STATE */
#define CMD1     (0x40+1)     	/* SEND_OP_COND */
#define CMD8     (0x40+8)     	/* SEND_IF_COND */
#define CMD9     (0x40+9)     	/* SEND_CSD */
#define CMD10    (0x40+10)    	/* SEND_CID */
#define CMD12    (0x40+12)    	/* STOP_TRANSMISSION */
#define CMD16    (0x40+16)    	/* SET_BLOCKLEN */
#define CMD17    (0x40+17)    	/* READ_SINGLE_BLOCK */
#define CMD18    (0x40+18)    	/* READ_MULTIPLE_BLOCK */
#define ACMD23   (0x40+23)    	/* SET_BLOCK_COUNT */
#define CMD24    (0x40+24)    	/* WRITE_BLOCK */
#define CMD25    (0x40+25)    	/* WRITE_MULTIPLE_BLOCK */
#define ACMD41   (0x40+41)    	/* SEND_OP_COND (ACMD) */
#define CMD55    (0x40+55)    	/* APP_CMD */
#define CMD58    (0x40+58)    	/* READ_OCR */

typedef uint8_t command_t;
typedef uint32_t argument_t;

typedef struct __attribute__((__packed__)) {
	command_t ind; /* command index */
	argument_t arg; /* command argument */
	uint8_t crc; /* command checksum: CRC7[7:1] stop bit[0] */
} SDMMC_CommandFrame;

typedef struct {
	uint8_t bit0Pos;
	uint8_t sliceLen;
} regSlice;

/* CSD Register structure
 * Card Specific Data (page 225)
 * format: {bit0Pos},{sliceLen} */
static const regSlice CSD_VER = {126, 2}; /* CSD structure version (00b) */
static const regSlice TAAC = {112, 8}; /* Data Read Access Time 1 */
static const regSlice NSAC = {104,8}; /* Data Read Access Time 2 in CLK (NSAC*100) */
static const regSlice TRAN_SPEED = {96,8}; /* MAX Data Transfer Rate */
static const regSlice CCC = {84,12}; /* Card Command Classes */
static const regSlice READ_BL_LEN = {80,4}; /* MAX Read Data Block Length */
static const regSlice READ_BL_PARTIAL = {79,1}; /* Partial block read allowed */
static const regSlice WRITE_BLK_MISALIGN = {78,1}; /* Block Write misalignment allowed */
static const regSlice READ_BLK_MISALIGN = {77,1}; /* Block Read misalignment allowed */
static const regSlice DSR_IMP = {76,1}; /* Driver Stage Register is present */
static const regSlice C_SIZE_v1 = {62,12}; /* Device Capacity in C_SIZE_MULT-s */
static const regSlice C_SIZE_v2 = {48,22}; /* Device Capacity in blocks */
static const regSlice C_SIZE_v3 = {48,28}; /* Device Capacity in blocks */
static const regSlice VDD_RD_CURR_MIN = {59,3}; /* MIN Read Current at Vdd MAX */
static const regSlice VDD_RD_CURR_MAX = {56,3}; /* MAX Read Current at Vdd MAX */
static const regSlice VDD_WR_CURR_MIN = {53,3}; /* MIN Write Current at Vdd MAX */
static const regSlice VDD_WR_CURR_MAX = {50,3}; /* MAX Write Current at Vdd MAX */
static const regSlice C_SIZE_MULT = {47,3}; /* Device Capacity Multiplier */
static const regSlice ERASE_BLK_EN = {46,1}; /* Erase Single Block Allowed */
static const regSlice ERASE_SECTOR_SIZE = {39,7}; /* Erase Sector Size */
static const regSlice WP_GRP_SIZE = {32,7}; /* Write Protect Group Size */
static const regSlice WP_GRP_ENABLE = {31,1}; /* Write Protect Group Enabled */
static const regSlice R2W_FACTOR = {26,3}; /* Write Speed Factor */
static const regSlice WRITE_BL_LEN = {22,4}; /* MAX Write Data Block Length */
static const regSlice WRITE_BL_PARTIAL = {21,1}; /* Partial Block Write Allowed */
static const regSlice FILE_FMT_GRP = {15,1}; /* File Format Group (OTP) */
static const regSlice COPY = {14,1}; /* Copy Flag (OTP) */
static const regSlice PERM_WP = {13,1}; /* Permanent Write Protection (OTP) */
static const regSlice TEMP_WP = {12,1}; /* Temporary Write Protection (R/W) */
static const regSlice FILE_FMT = {10,2}; /* File Format (OTP) */
static const regSlice WP_UPC = {9,1}; /* Write Protection Until Power Cycle (R/W if WP_UPC_SUPPORT) */
static const regSlice CRC7 = {1,7}; /* it's obvious */
static const regSlice STOP = {0,1}; /* Always 1 */

static const uint8_t dummy[16] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/***************************************
 * Helper functions
 **************************************/

uint8_t getCRC7(const uint8_t *data, uint32_t length) {
	const uint8_t poly = 0b10001001;
	uint8_t crc = 0;
	while (--length) {
		crc ^= *data++;
		for (uint8_t j = 0; j < 8; j++) {
			crc = (crc & 0x80u) ? ((crc << 1) ^ (poly << 1)) : (crc << 1);
		}
	}
	return crc | 1;
}

uint16_t getCRC16(const uint8_t *data, uint32_t length) {
	uint8_t x;
	uint16_t crc = 0xFFFF;

	while (length--) {
		x = crc >> 8 ^ *data++;
		x ^= x >> 4;
		crc = (crc << 8) ^ ((uint16_t) x << 12) ^ ((uint16_t) x << 5)
				^ (uint16_t) x;
	}
	return crc;
}

void bswap128(void* ptr) {
	uint32_t buf[4];
	memcpy(buf, ptr, 16);

	((uint32_t*)ptr)[0] = __builtin_bswap32(buf[3]);
	((uint32_t*)ptr)[1] = __builtin_bswap32(buf[2]);
	((uint32_t*)ptr)[2] = __builtin_bswap32(buf[1]);
	((uint32_t*)ptr)[3] = __builtin_bswap32(buf[0]);
}

uint32_t unpackReg(uint8_t * regPtr, regSlice rs) {
	uint32_t result = 0;

	regPtr += rs.bit0Pos / 8;
	rs.bit0Pos %= 8;

	result = (*(uint32_t*)regPtr >> rs.bit0Pos) & ~(UINT32_MAX << rs.sliceLen);

	return result;
}


/***************************************
 * Private methods
 **************************************/

/* There's no overflow check on CS_Lock counter.               *
 * The idea behind this is the SDMMC_select and SDMMC_deselect *
 * must always be in pairs.                                    */
void SDMMC_select(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	hsdmmc->CS_Lock++;
	HAL_GPIO_WritePin(hsdmmc->CS_GPIOx, hsdmmc->CS_GPIO_Pin, 0);
}

void SDMMC_deselect(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	hsdmmc->CS_Lock--;
	if (hsdmmc->CS_Lock == 0)
		HAL_GPIO_WritePin(hsdmmc->CS_GPIOx, hsdmmc->CS_GPIO_Pin, 1);
}

SDMMC_Result SDMMC_ReadyWait(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	uint32_t tickstart = HAL_GetTick();
	while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE)) {
		if ((HAL_GetTick() - tickstart) > hsdmmc->timeout) {
			return SDMMC_RES_ERROR;
		}
	}
	return SDMMC_RES_OK;
}

/* Also handles R1b */
SDMMC_Status SDMMC_receive_R1(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	uint8_t ncr = 8;
	SDMMC_Status sta;

	do {
		while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
			;
		sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy,
				&hsdmmc->response.R1.BYTE, 1, hsdmmc->timeout);
		if (sta != SM_OK)
			break;   //HAL error
		if (--ncr == 0) {
			sta = SM_ERROR;
			break;
		}
	} while (hsdmmc->response.R1.START);

	return sta;
}

/* not used by the currently supported command set */
//SDMMC_Status SDMMC_receive_R2(SDMMC_SPI_HandleTypeDef *hsdmmc) {
//	SDMMC_Status sta;
//
//	sta = SDMMC_receive_R1(hsdmmc);
//	if (sta == SM_OK) {
//		while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
//			;
//		sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy,
//				&hsdmmc->response.R2.BYTE, 1, hsdmmc->timeout);
//	}
//
//	return sta;
//}
SDMMC_Status SDMMC_receive_R3_R7(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	SDMMC_Status sta;
	uint32_t buf;

	sta = SDMMC_receive_R1(hsdmmc);
	if (sta == SM_OK) {
		while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
			;
		sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy,
				(uint8_t*) &buf, 4, hsdmmc->timeout);
		if (sta == SM_OK)
			hsdmmc->response.DWORD = __builtin_bswap32(buf);
	}

	return sta;
}

SDMMC_Status SDMMC_command(SDMMC_SPI_HandleTypeDef *hsdmmc, const command_t ind,
		const argument_t arg) {
	SDMMC_CommandFrame command;
	SDMMC_Status sta;

	if (hsdmmc->CS_Lock >= 2)
		return SM_BUSY;

	command.ind = ind;
	command.arg = __builtin_bswap32(arg);
//	command.crc = getCRC7((const uint8_t*)&command, sizeof(SDMMC_CommandFrame)-1);
	/* For some reason CRC7 gives incorrect value */
	/* Needs to be fixed later, hotfix below */

	switch (ind) {
	case CMD0:
		/* CRC for CMD0(0) */
		command.crc = 0x95;
		break;
	case CMD8:
		/* CRC for CMD8(0x1AA) */
		command.crc = 0x87;
		break;
	case ACMD23:
	case ACMD41:
		/* All "ACMD" command is a sequence of CMD55, CMD<n> */
		sta = SDMMC_command(hsdmmc, CMD55, 0);
		if (sta != SM_OK)
			return sta;
		//HAL error
		//break is omitted intentionally
	default:
		/* All other commands skip CRC check (by default but can be activated) */
		command.crc = 0x01;
	}

	SDMMC_select(hsdmmc);

	while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
		;
	sta = HAL_SPI_Transmit(hsdmmc->hspi, (uint8_t*) &command,
			sizeof(SDMMC_CommandFrame), hsdmmc->timeout);
	if (sta == SM_OK) {
		switch (ind) {
		case CMD8:
			sta = SDMMC_receive_R3_R7(hsdmmc);
			hsdmmc->response_type = RT_R7;
			break;
		case CMD58:
			sta = SDMMC_receive_R3_R7(hsdmmc);
			hsdmmc->response_type = RT_R3;
			break;
		default:
			sta = SDMMC_receive_R1(hsdmmc);
			hsdmmc->response_type = RT_R1;
		}
		if (sta == SM_OK) {
			if (hsdmmc->response.R1.BYTE & ~R1_IDLE)
				sta = HAL_ERROR;
		}
	}

	SDMMC_deselect(hsdmmc);
	return sta;
}

SDMMC_Status SDMMC_read_datablock(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t *buf,
		uint16_t size) {
	uint16_t retryCount = 0;
	uint16_t CRC16;
	SDMMC_Status sta;
	uint8_t token;

	/* Pooling for a valid Data Token */
	do {
		while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
			;
		retryCount++;
		sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy, &token, 1,
				hsdmmc->timeout);
	} while (sta == SM_OK && token == 0xff);
	if (sta != SM_OK) {
		hsdmmc->errorToken = token;
		return sta;
	}
	if (token != 0xfe) {
		hsdmmc->errorToken = token;
		return SM_ERROR;
	}

	/* Receiving data block (16 bytes at a time) */
	while (size) {
		uint16_t readSize;
		if (size > 16) {
			readSize = 16;
			size -= 16;
		} else {
			readSize = size;
			size = 0;
		}
		while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
			;
		sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy, buf,
				readSize, hsdmmc->timeout);
		if (sta != SM_OK)
			return sta;

		buf += 16;
	}

	/* Receive CRC but discarding it for now */
	while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
		;
	sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy,
			(uint8_t*) &CRC16, 2, hsdmmc->timeout);

	return sta;
}

//SDMMC_Status SDMMC_write_datablock(SDMMC_SPI_HandleTypeDef *hsdmmc, const uint8_t *buf, uint32_t size) {
//
//}

/***************************************
 * Public SDMMC methods
 **************************************/

SDMMC_State SDMMC_initialize(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	uint8_t retry;
	SDMMC_Status sta;

	if (hsdmmc->state != SMST_RESET) {
		return hsdmmc->state;
	}

	hsdmmc->state = SMST_BUSY;
	hsdmmc->CS_Lock = 1;
	SDMMC_deselect(hsdmmc);

	// TODO: set SPI clock between 100 and 400khz

	/* Resetting the SPI bus by sending 74 or more clock pulses while CS and MOSI both high */
	while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
		;
	sta = HAL_SPI_Transmit(hsdmmc->hspi, (uint8_t*) dummy, 10, hsdmmc->timeout);
	if (sta != SM_OK) {
		hsdmmc->state = SMST_RESET;
		return hsdmmc->state;   //HAL error
	}

	sta = SDMMC_command(hsdmmc, CMD0, 0);
	if (sta != SM_OK || hsdmmc->response.R1.BYTE != R1_IDLE) {
		hsdmmc->state = SMST_ERROR;
		return hsdmmc->state;   //Init error
	}

	/* DS2+ init */
	sta = SDMMC_command(hsdmmc, CMD8, 0x1aa);
	if (sta == SM_OK) {
		retry = hsdmmc->max_retry;
		do {
			sta = SDMMC_command(hsdmmc, ACMD41, 0x40000000);
			if (sta != SM_OK || --retry == 0) {
				hsdmmc->state = SMST_ERROR;
				return hsdmmc->state;   //Init error
			}
		} while (hsdmmc->response.R1.BYTE != 0);

		sta = SDMMC_command(hsdmmc, CMD58, 0);
		if (sta != SM_OK) {
			hsdmmc->state = SMST_ERROR;
			return hsdmmc->state; //Init error
		}
		hsdmmc->OCR = hsdmmc->response.OCR;
		if (hsdmmc->OCR.CCS) {
			hsdmmc->type = CT_SDHC;
			/* TODO detect CT_SDUC as well */
		} else {
			hsdmmc->type = CT_SD2;
		}
	} else {
		/* SD1 init */
		retry = hsdmmc->max_retry;
		do {
			sta = SDMMC_command(hsdmmc, ACMD41, 0);
		} while (hsdmmc->response.R1.BYTE != 0 && sta == SM_OK && --retry != 0);
		if (hsdmmc->response.R1.BYTE == 0) {
			hsdmmc->type = CT_SD1;
		} else {
			/* MMC init */
			retry = hsdmmc->max_retry;
			do {
				sta = SDMMC_command(hsdmmc, CMD1, 0);
				if (sta != SM_OK || --retry == 0) {
					hsdmmc->state = SMST_ERROR;
					return hsdmmc->state; //Init error
				}
			} while (hsdmmc->response.R1.BYTE != 0 && sta == SM_OK
					&& --retry != 0);
			if (hsdmmc->response.R1.BYTE == 0) {
				hsdmmc->type = CT_MMC;
			} else {
				hsdmmc->state = SMST_ERROR;
				return hsdmmc->state;
			}
		}
	}

	/* TODO set SPI clock back high according to IF_COND or OP_COND registers */
	/* but at least between 10Mhz and 20Mhz for MMC or 25Mhz for SDx */

	/* query additional important registers, save and parse them */

	/* Read CSD register */
	SDMMC_select(hsdmmc);

	sta = SDMMC_command(hsdmmc, CMD9, 0);
	if (sta != SM_OK) {
		hsdmmc->state = SMST_ERROR;
		return hsdmmc->state;
	} else {
		sta = SDMMC_read_datablock(hsdmmc, hsdmmc->CSD, 16);
		if (sta != SM_OK) {
			hsdmmc->state = SMST_ERROR;
			return hsdmmc->state;
		}
		bswap128(hsdmmc->CSD);
	}

	/* Read CID Register */
	sta = SDMMC_command(hsdmmc, CMD10, 0);
	if (sta != SM_OK) {
		hsdmmc->state = SMST_ERROR;
		goto end;
	} else {
		sta = SDMMC_read_datablock(hsdmmc, hsdmmc->CID, 16);
		if (sta != SM_OK) {
			hsdmmc->state = SMST_ERROR;
			goto end;
		}
		bswap128(hsdmmc->CID);
	}

	hsdmmc->CSD_ver = hsdmmc->type == CT_MMC ? 1 : (uint8_t)unpackReg(hsdmmc->CSD, CSD_VER) + 1;
	hsdmmc->blocklen_RD = (1 << (uint8_t)unpackReg(hsdmmc->CSD, READ_BL_LEN));
	hsdmmc->blocklen_WR = (1 << (uint8_t)unpackReg(hsdmmc->CSD, WRITE_BL_LEN));
	hsdmmc->sectorlen = (uint8_t)unpackReg(hsdmmc->CSD, ERASE_SECTOR_SIZE) + 1;
	if (hsdmmc->CSD_ver == 1) {
		/* (page 229) */
		hsdmmc->blockcount = ((uint16_t)unpackReg(hsdmmc->CSD, C_SIZE_v1) + 1)
				* (1 << ((uint8_t)unpackReg(hsdmmc->CSD, C_SIZE_MULT) + 2));
		hsdmmc->capacity = hsdmmc->blockcount * hsdmmc->blocklen_RD;
	} else {
		/* (page 234 / 238) */
		hsdmmc->capacity = (unpackReg(hsdmmc->CSD, C_SIZE_v3) + 1)
				* (512 * 1024);
		hsdmmc->blockcount = hsdmmc->capacity / hsdmmc->blocklen_RD;
	}

	hsdmmc->state = SMST_READY;
end:
	SDMMC_deselect(hsdmmc);
	return hsdmmc->state;
}

SDMMC_State SDMMC_get_state(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	return hsdmmc->state;
}

SDMMC_State SDMMC_read(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t *buff,
		uint32_t sector, uint32_t count) {
	// TODO

	return hsdmmc->state;
}

SDMMC_State SDMMC_write(SDMMC_SPI_HandleTypeDef *hsdmmc, const uint8_t *buff,
		uint32_t sector, uint32_t count) {
	// TODO

	return hsdmmc->state;
}

SDMMC_Result SDMMC_ioctl(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t ctrl,
		void *buff) {
	SDMMC_Result res = SDMMC_RES_OK;

	if (hsdmmc->state != SMST_READY) return RES_NOTRDY;

	switch (ctrl)
	{
	case CTRL_POWER:
//		DBGMSG("    CTRL_POWER: %hu\r\n", *ptr);
		switch (*(uint8_t*)buff) {
		case 0:
			/* Power Off */
			res = SDMMC_RES_OK;
			break;
		case 1:
			/* Power On */
			res = SDMMC_RES_OK;
			break;
		case 2:
			/* Power Check */
			res = SDMMC_RES_OK;
			break;
		default:
			res = SDMMC_RES_PARERR;
		}
		break;
	case GET_SECTOR_COUNT:
		*(uint32_t*) buff = hsdmmc->blockcount;
		break;
	case GET_SECTOR_SIZE:
		*(uint16_t*) buff = hsdmmc->blocklen_RD;
		res = SDMMC_RES_OK;
		break;
	case CTRL_SYNC:
		res = SDMMC_ReadyWait(hsdmmc);
		break;
	case MMC_GET_CSD:
		memcpy(buff, hsdmmc->CSD, 16);
		break;
	case MMC_GET_CID:
		memcpy(buff, hsdmmc->CID, 16);
		break;
	case MMC_GET_OCR:
		memcpy(buff, &hsdmmc->OCR, 4);
		break;
	default:
		res = SDMMC_RES_PARERR;
	}

	return res;
}

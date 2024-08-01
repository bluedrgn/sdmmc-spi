/* TODO write some fancy header with copyright information */
/**/
/**/
/**/
/**/
/**/
/**/
/**/

#include "sdmmc_spi.h"


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

typedef enum {
	SM_OK       = 0U,
	SM_ERROR    = 1U,
	SM_BUSY     = 2U,
	SM_TIMEOUT  = 3U
} SDMMC_Status;

typedef struct __attribute__((__packed__)) {
	command_t ind; /* command index */
	argument_t arg; /* command argument */
	uint8_t crc; /* command checksum: CRC7[7:1] stop bit[0] */
} SDMMC_CommandFrame;

static const uint8_t dummy[12] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff };

/***************************************
 * Helper functions
 **************************************/

uint8_t CRC7(const uint8_t *data, uint32_t length) {
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

uint16_t CRC16(const uint8_t *data, uint32_t length) {
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

/***************************************
 * Private methods
 **************************************/

void SDMMC_select(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	hsdmmc->Lock = SML_LOCKED;
	HAL_GPIO_WritePin(hsdmmc->CS_GPIOx, hsdmmc->CS_GPIO_Pin, 0);
}

void SDMMC_deselect(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	HAL_GPIO_WritePin(hsdmmc->CS_GPIOx, hsdmmc->CS_GPIO_Pin, 1);
	hsdmmc->Lock = SML_UNLOCKED;
}

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

SDMMC_Status SDMMC_receive_R2(SDMMC_SPI_HandleTypeDef *hsdmmc) {
	SDMMC_Status sta;

	sta = SDMMC_receive_R1(hsdmmc);
	if (sta == SM_OK) {
		while (!__HAL_SPI_GET_FLAG(hsdmmc->hspi, SPI_FLAG_TXE))
			;
		sta = HAL_SPI_TransmitReceive(hsdmmc->hspi, (uint8_t*) dummy,
				&hsdmmc->response.R2.BYTE, 1, hsdmmc->timeout);
	}

	return sta;
}

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

SDMMC_Status SDMMC_command(SDMMC_SPI_HandleTypeDef *hsdmmc,
		const command_t ind, const argument_t arg) {
	SDMMC_CommandFrame command;
	SDMMC_Status sta;

	if (hsdmmc->Lock == SML_LOCKED)
		return SM_BUSY;

	command.ind = ind;
	command.arg = __builtin_bswap32(arg);
//	command.crc = CRC7((const uint8_t*)&command, sizeof(SDMMC_CommandFrame)-1);
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
			/* TODO eliminate this goto somehow */
			goto end;
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

	end:
	SDMMC_deselect(hsdmmc);
	return sta;
}

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
	hsdmmc->Lock = SML_UNLOCKED;

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

	/* TODO query additional registers, save and parse  */

	hsdmmc->state = SMST_READY;
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

SDMMC_State SDMMC_ioctl(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t cmd,
		void *buff) {
	// TODO

	return hsdmmc->state;
}

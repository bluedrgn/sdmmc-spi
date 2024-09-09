/* TODO write some fancy header with copyright information */
/**/
/**/
/**/
/**/
/**/
/* https://web.archive.org/web/20210812130158/http://elm-chan.org/docs/mmc/mmc_e.html */

#ifndef SDMMC_SPI_H_
#define SDMMC_SPI_H_

#include "main.h"   /* For including the applicable HAL header */

/* R1 response flags */
#define R1_IDLE         0x01U   /* In Idle State */
#define R1_ERASE_RES    0x02U   /* Erase Reset */
#define R1_CMD_ERR      0x04U   /* Illegal Command */
#define R1_CRC_ERR      0x08U   /* Command CRC Error */
#define R1_ERASE_ERR    0x10U   /* Erase Sequence Error */
#define R1_ADDR_ERR     0x20U   /* Address Error */
#define R1_PARAM_ERR    0x40U   /* Parameter Error */
#define R1_START        0x80U   /* Start bit 0*/

/* Generic command (Used by FatFs) */
#define CTRL_SYNC		0	/* Complete pending write process (needed at _FS_READONLY == 0) */
#define GET_SECTOR_COUNT	1	/* Get media size (needed at _USE_MKFS == 1) */
#define GET_SECTOR_SIZE		2	/* Get sector size (needed at _MAX_SS != _MIN_SS) */
#define GET_BLOCK_SIZE		3	/* Get erase block size (needed at _USE_MKFS == 1) */
//#define CTRL_TRIM		4	/* Inform device that the data on the block of sectors is no longer used (needed at _USE_TRIM == 1) */

/* Generic command (Not used by FatFs) */
//#define CTRL_POWER			5	/* Get/Set power status */
//#define CTRL_LOCK			6	/* Lock/Unlock media removal */
//#define CTRL_EJECT			7	/* Eject media */
//#define CTRL_FORMAT			8	/* Create physical format on the media */

/* MMC/SDC specific ioctl command */
#define MMC_GET_TYPE		10	/* Get card type */
#define MMC_GET_CSD			11	/* Get CSD */
#define MMC_GET_CID			12	/* Get CID */
#define MMC_GET_OCR			13	/* Get OCR */
#define MMC_GET_SDSTAT		14	/* Get SD status */

/* ATA/CF specific ioctl command */
//#define ATA_GET_REV			20	/* Get F/W revision */
//#define ATA_GET_MODEL		21	/* Get model name */
//#define ATA_GET_SN			22	/* Get serial number */

typedef enum {
	CT_UNKNOWN = 0U,
	CT_MMC = 1U, /* MMC ver 3 */
	CT_SD1 = 2U, /* SD ver 1 */
	CT_SD2 = 3U, /* SD ver 2 */
	CT_SDHC = 5U, /* High Capacity or Extended Capacity (Block addressing) */
	CT_SDUC = 6U /* Ultra Capacity: (Block Addressing) */
} SDMMC_CardType;

typedef enum {
	SMST_RESET = 0U,
	SMST_READY = 1U,
	SMST_BUSY  = 2U,
	SMST_ERROR = 3U
} SDMMC_State;

typedef enum {
	SM_OK = 0U,
	SM_ERROR = 1U,
	SM_BUSY = 2U,
	SM_TIMEOUT = 3U
} SDMMC_Status;

/* Results of SDMMC Functions */
typedef enum {
	SDMMC_RES_OK = 0, /* 0: Successful */
	SDMMC_RES_ERROR, /* 1: R/W Error */
	SDMMC_RES_WRPRT, /* 2: Write Protected */
	SDMMC_RES_NOTRDY, /* 3: Not Ready */
	SDMMC_RES_PARERR /* 4: Invalid Parameter */
} SDMMC_Result;

typedef union __attribute__((__packed__)) {
	struct {
		uint8_t IDLE :1; /* In Idle State */
		uint8_t ERASE_RES :1; /* Erase Reset */
		uint8_t CMD_ERR :1; /* Illegal Command */
		uint8_t CRC_ERR :1; /* Command CRC Error */
		uint8_t ERASE_ERR :1; /* Erase Sequence Error */
		uint8_t ADDR_ERR :1; /* Address Error */
		uint8_t PARAM_ERR :1; /* Parameter Error */
		uint8_t START :1; /* Always 0 in a valid response*/
	};
	uint8_t BYTE;
} SDMMC_Response_R1;

typedef union __attribute__((__packed__)) {
	struct {
		uint8_t LOCKED :1; /* Card is locked */
		uint8_t ERA_LOC_FAIL :1; /* WP erase skip | lock/unlock cmd failed */
		uint8_t ERROR :1; /* ? */
		uint8_t CC_ERR :1; /* ? */
		uint8_t ECC_FAIL :1; /* Card ECC failed */
		uint8_t WP_VIOL :1; /* WP violation */
		uint8_t ERASE_PAR :1; /* Erase param */
		uint8_t OOR :1; /* Out of range */
	};
	uint8_t BYTE;
} SDMMC_Response_R2;

typedef struct __attribute__((__packed__)) {
	uint16_t :15; /* reserved */
	uint8_t V27_28A :1; /* Vcc 2.7-2.8V accepted */
	uint8_t V28_29A :1; /* Vcc 2.8-2.9V accepted */
	uint8_t V29_30A :1; /* Vcc 2.9-3.0V accepted */
	uint8_t V30_31A :1; /* Vcc 3.0-3.1V accepted */
	uint8_t V31_32A :1; /* Vcc 3.1-3.2V accepted */
	uint8_t V32_33A :1; /* Vcc 3.2-3.3V accepted */
	uint8_t V33_34A :1; /* Vcc 3.3-3.4V accepted */
	uint8_t V34_35A :1; /* Vcc 3.4-3.5V accepted */
	uint8_t V35_36A :1; /* Vcc 3.5-3.6V accepted */
	uint8_t S18A :1; /* Switching to 1.8V Accepted */
	uint8_t :2; /* reserved */
	uint8_t CO2T :1; /* Over 2TB support status */
	uint8_t :1; /* reserved */
	uint8_t UHSIICS :1; /* UHS-II Card Status */
	uint8_t CCS :1; /* Card Capacity Status */
	uint8_t busy :1;
} SDMMC_OCR_Reg;

typedef struct __attribute__((__packed__)) {
	SDMMC_Response_R1 R1;
	union {
		SDMMC_Response_R2 R2;
		SDMMC_OCR_Reg OCR;
		struct {
			uint8_t check_pattern;
			uint8_t voltage_accepted :4;
			uint16_t :16;
			uint8_t command_version :4;
		} R7;
		uint32_t DWORD;
	};
} SDMMC_Response;

typedef enum {
	RT_R1 = 1U, /* R1b also handled by SDMMC_receive_R1 */
//	RT_R2 = 2U,   /* not used by the currently supported command set */
	RT_R3 = 3U, /* only used by CMD58 */
	RT_R7 = 7U, /* only used by CMD8 */
} SDMMC_ResponseType;

typedef struct __SDMMC_SPI_HandleTypeDef {
	SPI_HandleTypeDef *hspi; /* HAL_SPI Handle for card interfacing bus */
	GPIO_TypeDef *CS_GPIOx; /* CE (Chip Enable (aka. Slave Select)) HAL_GPIO Handle */
	uint16_t CS_GPIO_Pin; /* CE GPIO pin */
	uint8_t CS_Lock; /* Providing thread safety (not tested) */
	uint32_t timeout; /* Operation time limit in systicks */
	uint8_t max_retry; /* Command maximum retry count before fail */
	uint8_t errorToken; /* Last error token returned by a data transfer */
	uint8_t responseToken; /* Data Response of last data transfer */
	SDMMC_CardType type; /* Type of memory card for handling protocol differences */
	uint32_t OP_COND; /* Operational Conditions */
	uint32_t IF_COND; /* Interface Condition */
	SDMMC_OCR_Reg OCR; /* Operation Conditions Register (page 222) */
	uint8_t CID[16]; /* Card Identification Register (page 224) */
//	uint16_t RCA[; /* Relative Card Address Register */
//	uint16_t DSR[; /* Driver Stage Register */
	uint8_t CSD[16]; /* Card Specific Data (page 225) */
	uint8_t CSD_ver; /* CSD register version */
//	uint32_t SCR[2]; /* SD Configuration Register */
	uint16_t blocklen_RD; /* Size of transfer data blocks and also the default logical sector size */
	uint16_t blocklen_WR; /* Same as blocklen_RD but for write transfers */
	uint32_t blockcount; /* SDMMC memory capacity in blocks */
	uint64_t capacity; /* SDMMC memory capacity in bytes */
	uint8_t sectorlen; /* Size of an erasable sector in blocks */
	SDMMC_State state; /* An internal state of the driver, hopefully prevents corruption */
	SDMMC_ResponseType response_type; /* Type of the last command response */
	SDMMC_Response response; /* Response from the last applied command */
//	uint32_t sectorAddress;
//	uint32_t sectorCount;
//	uint8_t *RXbuff;
//	const uint8_t *TXbuff;
} SDMMC_SPI_HandleTypeDef;

/* Public high level SDMMC Functions */
SDMMC_State SDMMC_initialize(SDMMC_SPI_HandleTypeDef *hsdmmc);
SDMMC_State SDMMC_get_state(SDMMC_SPI_HandleTypeDef *hsdmmc);
SDMMC_State SDMMC_read(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t *buff,
		uint32_t sector, uint32_t count);
SDMMC_State SDMMC_write(SDMMC_SPI_HandleTypeDef *hsdmmc, const uint8_t *buff,
		uint32_t sector, uint32_t count);
SDMMC_Result SDMMC_ioctl(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t cmd,
		void *buff);
/* TODO docstring style function desctiptions would be nice */

#endif /* SDMMC_SPI_H_ */

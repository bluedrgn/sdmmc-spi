/* TODO write some fancy header with copyright information */
/**/
/**/
/**/
/**/
/**/
/**/
/**/

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


typedef enum {
	CT_UNKNOWN = 0U,
	CT_MMC = 1U,   /* MMC ver 3 */
	CT_SD1 = 2U,   /* SD ver 1 */
	CT_SD2 = 4U,   /* SD ver 2+ */
	CT_SDC = 6U,   /* SD ??? */
	CT_SDHC = 8U   /* High Capacity SD: Block addressing */
} SDMMC_CardType;


typedef enum {
	SMST_RESET = 0U,
	SMST_READY = 1U,
	SMST_BUSY  = 2U,
	SMST_ERROR = 3U
} SDMMC_State;


typedef enum {
	SML_UNLOCKED = 0U,
	SML_LOCKED   = 1U
} SDMMC_Lock;


typedef union {
	struct {
		uint8_t IDLE :1;       /* In Idle State */
		uint8_t ERASE_RES :1;  /* Erase Reset */
		uint8_t CMD_ERR :1;    /* Illegal Command */
		uint8_t CRC_ERR :1;    /* Command CRC Error */
		uint8_t ERASE_ERR :1;  /* Erase Sequence Error */
		uint8_t ADDR_ERR :1;   /* Address Error */
		uint8_t PARAM_ERR :1;  /* Parameter Error */
		uint8_t START :1;      /* Always 0 in a valid response*/
	};
	uint8_t BYTE;
} SDMMC_Response_R1;


typedef union {
	struct {
		uint8_t LOCKED :1;        /* Card is locked */
		uint8_t ERA_LOC_FAIL :1;  /* WP erase skip | lock/unlock cmd failed */
		uint8_t ERROR :1;         /* ? */
		uint8_t CC_ERR :1;        /* ? */
		uint8_t ECC_FAIL :1;      /* Card ECC failed */
		uint8_t WP_VIOL :1;       /* WP violation */
		uint8_t ERASE_PAR :1;     /* Erase param */
		uint8_t OOR :1;           /* Out of range */
	};
	uint8_t BYTE;
} SDMMC_Response_R2;


typedef struct {
		uint16_t :15;        /* reserved */
		uint8_t V27_28A :1;  /* Vcc 2.7-2.8V accepted */
		uint8_t V28_29A :1;  /* Vcc 2.8-2.9V accepted */
		uint8_t V29_30A :1;  /* Vcc 2.9-3.0V accepted */
		uint8_t V30_31A :1;  /* Vcc 3.0-3.1V accepted */
		uint8_t V31_32A :1;  /* Vcc 3.1-3.2V accepted */
		uint8_t V32_33A :1;  /* Vcc 3.2-3.3V accepted */
		uint8_t V33_34A :1;  /* Vcc 3.3-3.4V accepted */
		uint8_t V34_35A :1;  /* Vcc 3.4-3.5V accepted */
		uint8_t V35_36A :1;  /* Vcc 3.5-3.6V accepted */
		uint8_t S18A :1;     /* Switching to 1.8V Accepted */
		uint8_t :2;          /* reserved */
		uint8_t CO2T :1;     /* Over 2TB support status */
		uint8_t :1;          /* reserved */
		uint8_t UHSIICS :1;  /* UHS-II Card Status */
		uint8_t CCS :1;      /* Card Capacity Status */
		uint8_t busy :1;
} SDMMC_OCR;


typedef struct __attribute__((__packed__)) {
	SDMMC_Response_R1 R1;
	union {
		SDMMC_Response_R2 R2;
		SDMMC_OCR OCR;
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
	RT_R1 = 1U,
	RT_R2 = 2U,
	RT_R3 = 3U,   /* only used by CMD58 */
	RT_R7 = 7U,   /* only used by CMD8 */
	RT_R1b= 9U    /* only used by CMD12 */
} SDMMC_ResponseType;


typedef struct __SDMMC_SPI_HandleTypeDef {
	SPI_HandleTypeDef *hspi;   /* HAL_SPI Handle for card interfacing bus */
	GPIO_TypeDef *CS_GPIOx;    /* CE (Chip Enable (aka. Slave Select)) HAL_GPIO Handle */
	uint16_t CS_GPIO_Pin;      /* CE GPIO pin */
	SDMMC_Lock Lock;           /* Providing thread safety (not tested) */
	uint32_t timeout;          /* Operation time limit in systicks */
	uint8_t max_retry;         /* Command maximum retry count before fail */
	SDMMC_CardType type;       /* Type of memory card for handling protocol differences */
	uint32_t OP_COND;          /* Operational Conditions */
	uint32_t IF_COND;          /* Interface Condition */
	SDMMC_OCR OCR;             /* Operation Conditions Register (page 222) */
	/* May be used later  */
//	uint32_t CID[4];           /* Card Identification Register (page 224) */
//	uint16_t RCA;              /* Relative Card Address Register */
//	uint16_t DSR;              /* Driver Stage Register */
//	uint32_t CSD[4];           /* Card Specific Data (page 225) */
//	uint32_t SCR[2];           /* SD Configuration Register */
	uint16_t blocklen;         /* Size of transfer data blocks and also the default logical sector size */
	uint32_t capacity;         /* SDMMC memory capacity in bytes */
	SDMMC_State state;         /*  */
	SDMMC_ResponseType response_type;
	SDMMC_Response response;
//	uint32_t sectorAddress;
//	uint32_t sectorCount;
//	uint8_t *RXbuff;
//	const uint8_t *TXbuff;
} SDMMC_SPI_HandleTypeDef;


/* Public high level SDMMC Functions */
SDMMC_State SDMMC_initialize(SDMMC_SPI_HandleTypeDef *hsdmmc);
SDMMC_State SDMMC_get_state(SDMMC_SPI_HandleTypeDef *hsdmmc);
SDMMC_State SDMMC_read(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t *buff, uint32_t sector, uint32_t count);
SDMMC_State SDMMC_write(SDMMC_SPI_HandleTypeDef *hsdmmc, const uint8_t *buff, uint32_t sector, uint32_t count);
SDMMC_State SDMMC_ioctl(SDMMC_SPI_HandleTypeDef *hsdmmc, uint8_t cmd, void *buff);


#endif /* SDMMC_SPI_H_ */

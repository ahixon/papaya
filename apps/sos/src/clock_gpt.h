#ifndef _CLOCK_GPT_H_
#define _CLOCK_GPT_H_

#define IRQ_GPT	87

#define GPT_MEMMAP_BASE	0x02098000
#define GPT_MEMMAP_SIZE	0x28

struct gpt_regs {
	uint32_t control;
	uint32_t prescale;
	uint32_t status;
	uint32_t interrupt;
	uint32_t ocr[3];
	uint32_t icr1;
	uint32_t icr2;
	uint32_t counter;
};

#define BIT(x)    (1UL << (x))

/* Force Output Compare bits 29-31 */
#define CR_OM3(x) (((x) & 7) << 26)
#define CR_OM2(x) (((x) & 7) << 23)
#define CR_OM1(x) (((x) & 7) << 20)
#define CR_IM2(x) (((x) & 3) << 18)
#define CR_IM1(x) (((x) & 3) << 16)
#define CR_SWR		BIT(15)
#define CR_FRR		BIT(9)
#define CR_CLKSRC(x) (((x) & 7) << 6)
#define CR_STOPEN	BIT(5)
#define CR_DOZEEN	BIT(4)
#define CR_WAITEN	BIT(3)
#define CR_DBGEN	BIT(2)
#define CR_ENMOD	BIT(1)
#define CR_EN		BIT(0)

#define CLOCK_NONE			0
#define CLOCK_PERIPHERAL	1 /* 66.6 MHz */
#define CLOCK_HIGHFREQ		2 /* ?? */
#define CLOCK_EXTERNAL		3 
#define CLOCK_LOWFREQ		4 /* 32 kHz */
#define CLOCK_CRYSTALDIV	5 /* 3 MHz */
#define CLOCK_RESERVED		6
#define CLOCK_CRYSTAL 		7 /* 24 MHz */

#define IM_DISABLED			0
#define IM_RISING			1
#define IM_FALLING			2
#define IM_BOTH				3

#define IR_ROLLOVER	BIT(5)
#define IR_OC3		BIT(2)
#define IR_OC2		BIT(1)
#define IR_OC1		BIT(0)

#define OUTPUT_DISCONNECT	0
#define OUTPUT_TOGGLE		1
#define OUTPUT_CLEAR		2
#define OUTPUT_SET			3
#define OUTPUT_ACTIVELOW	7

#define SR_ROV 	BIT(5)
#define SR_OF3 	BIT(2)
#define SR_OF2 	BIT(1)
#define SR_OF1 	BIT(0)

// we have 3; reserve 1 for weird 100ms tick in M1 assessment
#define NUM_GPT_TIMERS	2

#endif
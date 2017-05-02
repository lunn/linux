/*
 * holtic_hi3593.c - HoltIC HI-3593 driver
 *
 * The HI-3593 from Holt Integrated Circuits is a CMOS integrated circuit
 * for interfacing a Serial Peripheral Interface (SPI) enabled microcontroller
 * to the ARINC 429 serial bus.
 * This device has two receivers and single transmitter
 *
 * Copyright (C) 2015 Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/of_gpio.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <linux/arinc429/dev.h>

#define DRV_NAME "hi3593"

/* Default ARINC429 clock is 1MHz, but chip allows using clocks in 1-30MHz range
 * by using internal counter configured via ACLK DIV register */
#define DEFAULT_ARINC429_CLK_FREQ 1000000

#define RX_FIFO_SIZE 32
#define TX_FIFO_SIZE 32

/* max size for full FIFO bulk spi transfer */
#define SPI_RX_BUFFER_SIZE (RX_FIFO_SIZE * 4)

#define NUM_LABELS 256

/* Instruction opcodes */
/*------------------------------------------------------------------------------
 *					# Data
 *	ID			Value	bytes	Description
 *------------------------------------------------------------------------------*/
#define CMD_NOP_ZERO		0x00	/*  0	No operation			*/
#define CMD_MASTER_RESET	0x04	/*  0	Master Reset			*/
#define CMD_WRITE_TCR		0x08	/*  1	Write Transmit
						Control Register		*/
#define CMD_WRITE_MSG		0x0C	/*  4	Write ARINC 429 message
						to Transmit FIFO		*/
#define CMD_WRITE_R1_CR		0x10	/*  1	Write Receiver 1
						Control Register		*/
#define CMD_WRITE_R1_LABELS	0x14	/*  32	Write label values to Receiver 1
						label memory. Starting with
label 0xFF, consecutively set or reset each label in descending order.
For example, if the first data byte is programmed to 10110010 then labels
FF, FD FC and F9 will be set and FE, FB, FA and F8 will be reset.	*/

#define CMD_WRITE_R1_PLMR	0x18	/*  3	Write Receiver 1 Priority-Label
						Match Registers. The data field
consists of three eight-bit labels. The first data byte is written to
P-L filter #3, the second to P-L filter #2, and the last byte to filter #1	*/

#define CMD_WRITE_R2_CR		0x24	/*  1	Write Receiver 2
						Control Register		*/
#define CMD_WRITE_R2_LABELS	0x28	/*  32	Write label values to Receiver 2
						label memory. Starting with
label 0xFF, consecutively set or reset each label in descending order.
For example, if the first data byte is programmed to 10110010 then labels
FF, FD FC and F9 will be set and FE, FB, FA and F8 will be reset.		*/

#define CMD_WRITE_R2_PLMR	0x2C	/*  3	Write Receiver 2 Priority-Label
						Match Registers. The data field
consists of three eight-bit labels. The first eight bits is written to
P-L filter #3, the second to P-L filter #2, and the last byte to filter #1	*/

#define CMD_WRITE_FLAG		0x34	/*  1	Write Flag / Interrupt
						Assignment Register		*/
#define CMD_WRITE_ACLK_DIV	0x38	/*  1	Write ACLK Division Register	*/
#define CMD_TX			0x40	/*  0	Transmit current contents of
						Transmit FIFO if Transmit Control
						Register bit 5 (TMODE) is a "0"	*/
#define CMD_SOFT_RESET		0x44	/*  0	Software Reset. Clears the
						Transmit and Receive FIFOs and
						the Priority-Label Registers	*/
#define CMD_SET_R1_LABELS	0x48	/*  0	Set all bits in Receiver 1 label
						memory to a "1"			*/
#define CMD_SET_R2_LABELS	0x4C	/*  0	Set all bits in Receiver 2 label
						memory to a "1"			*/
#define CMD_READ_TSR		0x80	/*  1	Read Transmit Status Register	*/
#define CMD_READ_TCR		0x84	/*  1	Read Transmit Control Register	*/
#define CMD_READ_R1_STATUS	0x90	/*  1	Read Receiver 1 Status Register	*/
#define CMD_READ_R1_CTRL	0x94	/*  1	Read Receiver 1
						Control Register 		*/
#define CMD_READ_R1_LABELS	0x98	/*  32	Read label values from Receiver 1
						label memory. */
#define CMD_READ_R1_PLMR	0x9C	/*  3	Read Receiver 1 Priority-Label
						Match Registers.		*/
#define CMD_READ_R1_MSG		0xA0	/*  4	Read one ARINC 429 message from
						the Receiver 1 FIFO		*/
#define CMD_READ_R1_PLR1	0xA4	/*  3	Read Receiver 1 Priority-Label
						Register #1, ARINC429 bytes 2,3
						& 4 (bits 9 - 32)		*/
#define CMD_READ_R1_PLR2	0xA8	/*  3	Read Receiver 1 Priority-Label
						Register #2, ARINC429 bytes 2,3
						& 4 (bits 9 - 32)		*/
#define CMD_READ_R1_PLR3	0xAC	/*  3	Read Receiver 1 Priority-Label
						Register #3, ARINC429 bytes 2,3
						& 4 (bits 9 - 32)		*/
#define CMD_READ_R2_STATUS	0xB0	/*  1	Read Receiver 2 Status Register	*/
#define CMD_READ_R2_CTRL	0xB4	/*  1	Read Receiver 2
						Control Register		*/
#define CMD_READ_R2_LABELS	0xB8	/*  32	Read label values from
						Receiver 2 label memory.	*/
#define CMD_READ_R2_PLMR	0xBC	/*  3	Read Receiver 2 Priority-Label
						Match Registers.		*/
#define CMD_READ_R2_MSG		0xC0	/*  4	Read one ARINC 429 message from
						the Receiver 2 FIFO		*/
#define CMD_READ_R2_PLR1	0xC4	/*  3	Read Receiver 2 Priority-Label
						Register #1, ARINC429 bytes 2,3
						& 4 (bits 9 - 32)		*/
#define CMD_READ_R2_PLR2	0xC8	/*  3	Read Receiver 2 Priority-Label
						Register #2, ARINC429 bytes 2,3
						& 4 (bits 9 - 32)		*/
#define CMD_READ_R2_PLR3	0xCC	/*  3	Read Receiver 2 Priority-Label
						Register #3, ARINC429 bytes 2,3
						& 4 (bits 9 - 32)		*/
#define CMD_READ_FLAG		0xD0	/*  1	Read Flag / Interrupt Assignment
						Register			*/
#define CMD_READ_ACLK_DIV	0xD4	/*  1	Read ACLK Division Register	*/
#define CMD_NOP_ONES		0xFF	/*  0	No operation.			*/

/* FLAG / Interrupt assignment register bits */
#define R1_FLAG_FIFO_EMPTY	(0)	/* FIFO empty */
#define R1_FLAG_FIFO_FULL	(1)	/* FIFO contains 32 messages */
#define R1_FLAG_FIFO_HALF_FULL	(2)	/* FIFO contains 16 messages */
#define R1_FLAG_FIFO_NOT_EMPTY	(3)	/* FIFO is not empty */
#define R1_FLAG_MASK		(3)	/* mask bits for R1 FLAG settings */
#define R1_INT_MSG_RECEIVED	(0 << 2) /* any valid message received */
#define R1_INT_PL1_MSG_RECEIVED	(1 << 2) /* Message in Priority Label mailbox #1 */
#define R1_INT_PL2_MSG_RECEIVED	(2 << 2) /* Message in Priority Label mailbox #2 */
#define R1_INT_PL3_MSG_RECEIVED	(3 << 2) /* Message in Priority Label mailbox #3 */
#define R2_FLAG_FIFO_EMPTY	(0 << 4) /* same as above for second receiver */
#define R2_FLAG_FIFO_FULL	(1 << 4)
#define R2_FLAG_FIFO_HALF_FULL	(2 << 4)
#define R2_FLAG_FIFO_NOT_EMPTY	(3 << 4)
#define R2_FLAG_MASK		(3 << 4)
#define R2_INT_MSG_RECEIVED	(0 << 6)
#define R2_INT_PL1_MSG_RECEIVED	(1 << 6)
#define R2_INT_PL2_MSG_RECEIVED	(2 << 6)
#define R2_INT_PL3_MSG_RECEIVED	(3 << 6)

/* #define DEFAULT_FLAG_IAR_VALUE	(R1_INT_MSG_RECEIVED | \
				R1_FLAG_FIFO_HALF_FULL | \
				R2_INT_MSG_RECEIVED | \
				R2_FLAG_FIFO_HALF_FULL)  */
#define DEFAULT_FLAG_IAR_VALUE (R1_FLAG_FIFO_NOT_EMPTY | R2_FLAG_FIFO_NOT_EMPTY)

/* Receiver control register bits */
#define RECEIVER_FLIP			(1 << 7)
#define RECEIVER_SD9			(1 << 6)
#define RECEIVER_SD10			(1 << 5)
#define RECEIVER_SDON			(1 << 4)
#define RECEIVER_PARITY			(1 << 3)
#define RECEIVER_LABEL_FILTERS_ENABLED	(1 << 2)
#define RECEIVER_PRIORITY_MATCH_ON	(1 << 1)
#define RECEIVER_RATE			(1 << 0)

/* ARINC429 simulator sends data with flipped bits and parity enabled,
 * use these settings as default */
#define DEFAULT_RX_CR_VALUE	(RECEIVER_FLIP | RECEIVER_PARITY)

/* Receiver status register bits */
#define RECEIVER_STATUS_PL3		(1 << 5)
#define RECEIVER_STATUS_PL2		(1 << 4)
#define RECEIVER_STATUS_PL1		(1 << 3)
#define RECEIVER_STATUS_FIFO_FULL	(1 << 2)
#define RECEIVER_STATUS_FIFO_HALF_FULL	(1 << 1)
#define RECEIVER_STATUS_FIFO_EMPTY	(1 << 0)

/* Transmitter control register bits */
#define TRANSMITTER_HIZ			(1 << 7)
#define TRANSMITTER_FLIP		(1 << 6)
#define TRANSMITTER_MODE		(1 << 5)
#define TRANSMITTER_SELF_TEST		(1 << 4)
#define TRANSMITTER_ODD_EVEN		(1 << 3)
#define TRANSMITTER_PARITY		(1 << 2)
#define TRANSMITTER_RATE		(1 << 0)

/* flip label bits, send immediately after TX FIFO write and use odd parity */
#define DEFAULT_TX_CR_VALUE	(TRANSMITTER_FLIP | TRANSMITTER_MODE | TRANSMITTER_PARITY)

/* Transmitter status bits */
#define TRANSMITTER_STATUS_FULL		(1 << 2)
#define TRANSMITTER_STATUS_HALF_FULL	(1 << 1)
#define TRANSMITTER_STATUS_EMPTY	(1 << 0)


/* channel types */
enum channel_type {
	FIRST_CHANNEL,

	RECEIVER_1 = FIRST_CHANNEL,
	RECEIVER_2,
	NUM_RECEIVERS,

	TRANSMITTER = NUM_RECEIVERS,

	NUM_CHANNELS
};

const char const* channel_type_names[] = {
	__stringify(RECEIVER_1),
	__stringify(RECEIVER_2),
	__stringify(TRANSMITTER)
};

/* opcodes to write channel Control Register */
static const u8 write_cr_cmds[NUM_CHANNELS] =
	{CMD_WRITE_R1_CR, CMD_WRITE_R2_CR, CMD_WRITE_TCR};
/* opcodes to read channel Status Register */
static const u8 read_sr_cmds[NUM_CHANNELS] =
	{CMD_READ_R1_STATUS, CMD_READ_R2_STATUS, CMD_READ_TSR};
/* opcodes to receive/send message over channel */
static const u8 io_msg_cmds[NUM_CHANNELS] =
	{CMD_READ_R1_MSG, CMD_READ_R2_MSG, CMD_WRITE_MSG};

/* label flip register bits for each channel */
static const u8 label_flip_bits[NUM_CHANNELS] =
	{RECEIVER_FLIP, RECEIVER_FLIP, TRANSMITTER_FLIP};
/* rate register bits for each channel */
static const u8 rate_bits[NUM_CHANNELS] =
	{RECEIVER_RATE, RECEIVER_RATE, TRANSMITTER_RATE};
/* parity register bits for each channel */
static const u8 parity_bits[NUM_CHANNELS] =
	{RECEIVER_PARITY, RECEIVER_PARITY, TRANSMITTER_PARITY};


/* ARINC429 bus specifies two rates HIGH and LOW */
enum channel_rate {
	RATE_HIGH,
	RATE_LOW
};

struct hi3593_priv;

/* RX kernel thread (one per RX channel) that reads all available messages
 * from RX FIFO
 *
 * @task - task structure
 * @thread_done - flag to indicate end of execution
 * @rx - flag to indicate that one or more messages are available in FIFO
 * @wait - wait queue to sleep on
 * @completion - indicate that thread has finished its execution
 * @lock - protects rx flag setting in RX thread and IRQ handler
 * @irq_timeout - used to switch receive path back to single interrupt per
 *		  message for slow label rates
 */
struct hi3593_rx_thread {
	struct task_struct	*task;
	atomic_t		thread_done;
	atomic_t		rx;
	wait_queue_head_t	wait;
	struct completion	completion;
	spinlock_t		lock;
	struct timer_list	irq_timeout;
};

/* HoltIC HI-3953 single channel private data
 *
 * @arinc429 - common ARINC429 interface private data
 * @ndev: network device private data
 * @adev: HI-3593 chip specific private data
 * @type: channel type
 * @rate: channel rate
 * @work: workitem for TX message processing
 * @fifo_full: used to stop pushing messages to HW after TX FIFO FULL IRQ
 * @rx_thread: receiver thread
 * @last_irq_time: timestamp of last INT IRQ
 * @label_interval: interval between labels as measured by INT IRQ
 * @rx_irq_disabled: cached RxINT IRQ state
 * @flag_irq_disabled: cached RxFLAG IRQ state
 */
struct hi3593_channel_priv {
	struct arinc429_priv	arinc429;	/* must be the first member */
	struct net_device	*ndev;
	struct hi3593_priv	*adev;

	enum channel_type	type;
	enum channel_rate	rate;

	union {
		/* TX specific members */
		struct {
			struct work_struct	work;
			atomic_t		fifo_full;
		};
		/* RX specific members */
		struct {
			struct hi3593_rx_thread	rx_thread;
			u64			label_interval;
			u64			last_irq_time;
			bool			rx_irq_disabled;
			bool			flag_irq_disabled;
		};
	};
};

/* HoltIC HI-3953 chip specific private data
 *
 * @spi: spi device
 * @spi_buffer: data buffer for spi message
 * @spi_transfer: spi transfers for message
 * @dev_lock: lock protecting this structure
 * @active_channels: amount of opened channels
 * @labels: label for each channel provided via device tree
 * @channels: channels private data
 * @ctrl_regs: cached control registers for each channel
 * @flag_iar: cached FLAG/Interrupt Assignment Register
 * @r1_label_filter: label filter bitmap in direct order (from 0000 to 0377)
 * @r2_label_filter: same as above for second receiver, do not forget to
                     to reverse byte order befor writing into HW as it takes
                     label bitmap from 0377 to 0000
 * @hw_reset: external master reset GPIO pin
 * @rx_int: RxINT interrupt GPIO pins
 * @flag_int: RxFLAG interrupt GPIO pins
 * @tx_full: TFULL interrupt GPIO pin
 * @tx_empty: TFEMPTY interrupt GPIO pin
 * @freq: ARINC429 bus reference clock
 * @txq: TX queue
 * @txq_count: amount of messages in TX queue
 */
struct hi3593_priv {
	struct spi_device		*spi;
	u8				*spi_buffer;
	struct spi_transfer		spi_transfer[RX_FIFO_SIZE * 2];

	struct mutex			dev_lock;
	int				active_channels;
	const char*			labels[NUM_CHANNELS];
	struct hi3593_channel_priv	*channels[NUM_CHANNELS];
	u8				ctrl_regs[NUM_CHANNELS];
	u8				flag_iar;
	DECLARE_BITMAP(			r1_label_filter, NUM_LABELS);
	DECLARE_BITMAP(			r2_label_filter, NUM_LABELS);
	int				hw_reset;
	int				rx_int[NUM_RECEIVERS];
	int				flag_int[NUM_RECEIVERS];
	int				tx_full;
	int				tx_empty;
	u32				freq;
	struct sk_buff_head		txq;
	atomic_t		txq_count;
};

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *debugfs_rootdir = NULL;
static int num_devices = 0;
static DEFINE_MUTEX(debug_lock);

static struct {
	char const* name;
	u8 reg;
	u8 data_size;
} read_regs[] = {
	{"TX SR",	CMD_READ_TSR,		1},
	{"TX CR",	CMD_READ_TCR,		1},
	{"RX1 SR",	CMD_READ_R1_STATUS,	1},
	{"RX1 CR",	CMD_READ_R1_CTRL,	1},
	{"RX1 labels",	CMD_READ_R1_LABELS,	32},
	{"RX1 PLMR",	CMD_READ_R1_PLMR,	3},
	{"RX1 MSG",	CMD_READ_R1_MSG,	4},
	{"RX1 PLR1",	CMD_READ_R1_PLR1,	3},
	{"RX1 PLR2",	CMD_READ_R1_PLR2,	3},
	{"RX1 PLR3",	CMD_READ_R1_PLR3,	3},
	{"RX2 SR",	CMD_READ_R2_STATUS,	1},
	{"RX2 CR",	CMD_READ_R2_CTRL,	1},
	{"RX2 labels",	CMD_READ_R2_LABELS,	32},
	{"RX2 PLMR",	CMD_READ_R2_PLMR,	3},
	{"RX2 MSG",	CMD_READ_R2_MSG,	4},
	{"RX2 PLR1",	CMD_READ_R2_PLR1,	3},
	{"RX2 PLR2",	CMD_READ_R2_PLR2,	3},
	{"RX2 PLR3",	CMD_READ_R2_PLR3,	3},
	{"FLAG/IAR",	CMD_READ_FLAG,		1},
	{"ACLK DIV", 	CMD_READ_ACLK_DIV,	1}
};

#define READ_REGS_COUNT (sizeof(read_regs) / sizeof(read_regs[0]))

static inline void hi3593_reverse_bytes(u8 *dst,
		const u8 *src, unsigned int n);

static int hi3593_regs_dump(struct seq_file *s, void *data)
{
	struct hi3593_priv *adev = s->private;
	DECLARE_BITMAP(reversed, NUM_LABELS);
	u8 rx_buf[32];
	int ret;
	int i;

	mutex_lock(&adev->dev_lock);

	for (i = 0; i < READ_REGS_COUNT; i++) {
		ret = spi_write_then_read(adev->spi,
			&read_regs[i].reg, 1,
			rx_buf, read_regs[i].data_size);

		if (ret)
			goto exit;
		switch(read_regs[i].data_size) {
		case 1:
			seq_printf(s,"%s : 0x%02X\n",
					read_regs[i].name, rx_buf[0]);
			break;
		case 3:
			seq_printf(s,"%s : 0x%02X 0x%02X 0x%02X\n",
					read_regs[i].name, rx_buf[0],
					rx_buf[1], rx_buf[2]);
			break;
		case 4:
			seq_printf(s,"%s : 0x%02X 0x%02X 0x%02X 0x%02X\n",
					read_regs[i].name, rx_buf[0],
					rx_buf[1], rx_buf[2], rx_buf[3]);
			break;

		case 32:
			hi3593_reverse_bytes((u8*)reversed,
				(const u8*)rx_buf, NUM_LABELS / BITS_PER_BYTE);
			seq_printf(s,"%s : %*pb\nas list: {%*pbl}\n",
				read_regs[i].name, NUM_LABELS, reversed,
				NUM_LABELS, reversed);
			break;

		default:
			break;
		}
	}
exit:
	mutex_unlock(&adev->dev_lock);

	return ret;
}

static int hi3593_regs_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, hi3593_regs_dump, inode->i_private);
}

static const struct file_operations hi3593_debugfs_fops = {
	.open		= hi3593_regs_dump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void hi3593_register_debugfs(struct hi3593_priv *adev)
{
	struct dentry *dev_dir;
	struct dentry *regs_file;
	char dev_name[4];

	mutex_lock(&debug_lock);

	if (!debugfs_rootdir) {
		debugfs_rootdir = debugfs_create_dir(DRV_NAME, NULL);
		if (!debugfs_rootdir)
			goto unlock;
	}

	snprintf(dev_name, sizeof(dev_name), "%u", num_devices);
	num_devices++;

	dev_dir = debugfs_create_dir(dev_name, debugfs_rootdir);
	if (!dev_dir) {
		dev_dbg(&adev->spi->dev, "unable to create dir: %s\n", dev_name);
		goto unlock;
	}

	regs_file = debugfs_create_file("regs", S_IRUGO, dev_dir,
				adev, &hi3593_debugfs_fops);

unlock:
	mutex_unlock(&debug_lock);
}

static void hi3593_unregister_debugfs(struct hi3593_priv *adev)
{
	mutex_lock(&debug_lock);
	num_devices--;
	if (!num_devices) {
		debugfs_remove_recursive(debugfs_rootdir);
		debugfs_rootdir = NULL;
	}

	mutex_unlock(&debug_lock);
}

#else
static inline void hi3593_register_debugfs(struct hi3593_priv *adev) {}
static inline void hi3593_unregister_debugfs(struct hi3593_priv *adev) {}
#endif

static inline bool hi3593_is_transmitter(enum channel_type channel_type)
{
	return channel_type == TRANSMITTER;
}

static inline bool hi3593_is_receiver(enum channel_type channel_type)
{
	return !(hi3593_is_transmitter(channel_type));
}

static inline int hi3593_is_receive_fifo_empty(
			struct hi3593_channel_priv *chan)
{
	u8 status;
	int ret;

	ret = spi_write_then_read(chan->adev->spi,
			&read_sr_cmds[chan->type], 1, &status, 1);
	if (ret) {
		netdev_err(chan->ndev, "unable to get Receiver status\n");
		return ret;
	}

	if (status & RECEIVER_STATUS_FIFO_EMPTY)
		return 1;

	return 0;
}

static inline int __hi3593_write_ctrl_reg(struct hi3593_priv *adev,
		enum channel_type channel_type, u8 val)
{
	u8 tx_buf[2];
	int ret;

	dev_vdbg(&adev->spi->dev,
		"%s: %s CTRL = %#x\n",
		__func__, channel_type_names[channel_type], val);

	tx_buf[0] = write_cr_cmds[channel_type];
	tx_buf[1] = val;

	ret = spi_write(adev->spi, tx_buf, sizeof(tx_buf));

	/* update cached copy after successfull HW configuration */
	if (!ret)
		adev->ctrl_regs[channel_type] = val;

	return ret;
}

static inline int __hi3593_set_ctrl_reg_bits(struct hi3593_priv *adev,
		enum channel_type channel_type, u8 bits)
{
	return __hi3593_write_ctrl_reg(adev, channel_type,
			adev->ctrl_regs[channel_type] | bits);
}

static inline int __hi3593_clear_ctrl_reg_bits(struct hi3593_priv *adev,
		enum channel_type channel_type, u8 bits)
{
	return __hi3593_write_ctrl_reg(adev, channel_type,
			adev->ctrl_regs[channel_type] & ~bits);
}

static void hi3593_tx_work_handler(struct work_struct *ws)
{
	struct hi3593_channel_priv *chan = container_of(ws,
			struct hi3593_channel_priv, work);
	struct hi3593_priv *adev = chan->adev;
	struct spi_device *spi = adev->spi;
	struct sk_buff *txb;
	bool last = skb_queue_empty(&adev->txq);

	netdev_vdbg(chan->ndev, "%s: enter\n", __func__);

	while (!last && !atomic_read(&chan->fifo_full)) {
		txb = skb_dequeue(&adev->txq);
		last = skb_queue_empty(&adev->txq);
		if (txb) {
			struct net_device *ndev = txb->dev;
			struct arinc429_frame *af =
				(struct arinc429_frame *)txb->data;
			u8 tx_buf[5] = { CMD_WRITE_MSG, 0, 0, 0, 0 };
			int ret;

			tx_buf[1] = af->data[0];
			tx_buf[2] = af->data[1];
			tx_buf[3] = af->data[2];
			tx_buf[4] = af->label;

			netdev_vdbg(ndev,
				"tx message label: %#o, data: %#x %02x %02x\n",
				af->label, af->data[0], af->data[1], af->data[2]);

			/* Load the TX buffer into the hardware. */
			ret = spi_write(spi, tx_buf, sizeof(tx_buf));
			if (ret) {
				ndev->stats.tx_errors++;
			} else {
				ndev->stats.tx_packets++;
				ndev->stats.tx_bytes += sizeof(*af);
			}

			dev_kfree_skb(txb);
			atomic_dec(&adev->txq_count);

			netdev_vdbg(ndev, "packet sent, label: %#o ret: %d\n",
				af->label, ret);
		}
	}

	/* ok, HW accepted all queued buffers, allow more data to be sent */
	if (last && netif_queue_stopped(chan->ndev))
		netif_wake_queue(chan->ndev);
}

static int hi3593_master_reset(struct hi3593_priv *adev)
{
	int ret = 0;

	dev_dbg(&adev->spi->dev, "%s: enter\n", __func__);

	/* MR must be pulsed high for 1us to bring the part to
	 * its completely reset state. */

	if (gpio_is_valid(adev->hw_reset)) {
		gpio_set_value_cansleep(adev->hw_reset, 1);
		udelay(1);
		gpio_set_value_cansleep(adev->hw_reset, 0);
	} else {
		/* perform master reset via SPI command */
		u8 tx_buf = CMD_MASTER_RESET;
		ret = spi_write(adev->spi, &tx_buf, 1);
		udelay(1);
	}

	return ret;
}

static int hi3593_set_clock(struct spi_device *spi, u32 desired_freq)
{
	dev_dbg(&spi->dev, "%s: enter\n", __func__);

	if (desired_freq != DEFAULT_ARINC429_CLK_FREQ) {
		u8 tx_buf[2] = { CMD_WRITE_ACLK_DIV, 0x02 };
		u32 freq = 2 * DEFAULT_ARINC429_CLK_FREQ;

		/* HI-3593 clk should be even multiple of 1 MHz (up to 30 MHz) */
		do {
			if (freq == desired_freq) {
				dev_dbg(&spi->dev,
					"setting clock to %u Hz\n", freq);
				return spi_write(spi, tx_buf, 2);
			}

			freq += 2 * DEFAULT_ARINC429_CLK_FREQ;
			tx_buf[1] += 2;
		} while (freq <= 30 * DEFAULT_ARINC429_CLK_FREQ);

		return -EINVAL;
	}

	return 0;
}

/* Reads single message from RX FIFO, make sure that FIFO contains
 * a message. HW does not report error on try to read empty FIFO.
 */
static void hi3593_rx_buf(struct hi3593_channel_priv *chan)
{
	struct spi_device *spi = chan->adev->spi;
	struct sk_buff *skb;
	struct arinc429_frame *af;
	u8 rx_buf[4] = {0};
	int ret;
	
	netdev_vdbg(chan->ndev, "%s: enter\n", __func__);

	ret = spi_write_then_read(spi, &io_msg_cmds[chan->type], 1, rx_buf, 4);
	if (ret) {
		chan->ndev->stats.rx_errors++;
		netdev_err(chan->ndev, "unable to read message\n");
		return;
	}

	skb = alloc_arinc429_skb(chan->ndev, &af);
	if (skb == NULL) {
		netdev_err(chan->ndev, "unable to allocate skb\n");
		chan->ndev->stats.rx_dropped++;
		return;
	}

	af->data[0] = rx_buf[0];
	af->data[1] = rx_buf[1];
	af->data[2] = rx_buf[2];
	af->label = rx_buf[3];

	netif_rx_ni(skb);

	chan->ndev->stats.rx_packets++;
	chan->ndev->stats.rx_bytes += sizeof(*af);

	netdev_vdbg(chan->ndev,
		"received message label: %#o, data: %#x %02x %02x\n",
		af->label, af->data[0], af->data[1], af->data[2]);
}

/* Reads messages from RX FIFO until it is empty or thread exit requested
 *
 * returns 0 if nothing could be read from FIFO,
 * negative value in case of error or thread exit
 */
static int hi3593_receive(struct hi3593_channel_priv *chan)
{
	int msg_count;
	u8 status;
	int ret;

	netdev_vdbg(chan->ndev, "%s: enter\n", __func__);

	msg_count = 0;

	ret = spi_write_then_read(chan->adev->spi,
			&read_sr_cmds[chan->type], 1, &status, 1);
	if (ret) {
		netdev_err(chan->ndev, "unable to get Receiver status\n");
		return ret;
	}

	if (status & RECEIVER_STATUS_FIFO_EMPTY)
		return 0;

	/* try to read as many messages as possible without status bit check */
	if (status & RECEIVER_STATUS_FIFO_FULL) {
		msg_count = RX_FIFO_SIZE;
	} else	if (status & RECEIVER_STATUS_FIFO_HALF_FULL) {
		msg_count = RX_FIFO_SIZE / 2;
	} else if ((!status) & RECEIVER_STATUS_FIFO_EMPTY) {
		msg_count = 1;
	}

	if (likely(msg_count)) {
		u8 i;
		unsigned long flags;

		/* as soon as FIFO half full or FIFO full is detected, disable single message interrupt */
		spin_lock_irqsave(&chan->rx_thread.lock, flags);
		if (!chan->rx_irq_disabled) {
			disable_irq_nosync(gpio_to_irq(chan->adev->rx_int[chan->type]));
			chan->rx_irq_disabled = true;
			netdev_vdbg(chan->ndev,
				    "%s: disabled RxINT IRQ on channel %d\n",
				    __func__, chan->type);
		}
		spin_unlock_irqrestore(&chan->rx_thread.lock, flags);

		if (likely(chan->adev->spi_buffer)) {
			struct spi_message	message;

			spi_message_init(&message);
			memset(chan->adev->spi_buffer, 0, msg_count * 4);
			memset(chan->adev->spi_transfer, 0, sizeof(chan->adev->spi_transfer[0]) * msg_count);
			for (i = 0; i < msg_count; i++) {
				chan->adev->spi_transfer[i * 2].len = 1;
				chan->adev->spi_transfer[i * 2 + 1].len = 4;
				chan->adev->spi_transfer[i * 2 + 1].cs_change = (i < msg_count - 1);

				spi_message_add_tail(&chan->adev->spi_transfer[i * 2], &message);
				spi_message_add_tail(&chan->adev->spi_transfer[i * 2 + 1], &message);

				chan->adev->spi_transfer[i * 2].tx_buf = &io_msg_cmds[chan->type];
				chan->adev->spi_transfer[i * 2 + 1].rx_buf = &chan->adev->spi_buffer[i * 4];
			}

			ret = spi_sync(chan->adev->spi, &message);
			if (ret)
				return ret;

			for (i = 0; i < msg_count; i++) {
				struct sk_buff *skb;
				struct arinc429_frame *af;

				skb = alloc_arinc429_skb(chan->ndev, &af);
				if (skb == NULL) {
					netdev_err(chan->ndev, "unable to allocate skb\n");
					chan->ndev->stats.rx_dropped++;
					return -EPIPE;
				}

				af->data[0] = chan->adev->spi_buffer[i * 4];
				af->data[1] = chan->adev->spi_buffer[i * 4 + 1];
				af->data[2] = chan->adev->spi_buffer[i * 4 + 2];
				af->label = chan->adev->spi_buffer[i * 4 + 3];

				netif_rx_ni(skb);

				chan->ndev->stats.rx_packets++;
				chan->ndev->stats.rx_bytes += sizeof(*af);
			}

			return 0;
		}
		else {
			for (i = 0; i < msg_count; i++) {
				hi3593_rx_buf(chan);
				if (unlikely(atomic_read(&chan->rx_thread.thread_done)))
					return -EPIPE;
			}
		}
		return 0;
	}

	/* there is no indication of how many messages are in RX FIFO.
	 * It is known that at least one is there */
	hi3593_rx_buf(chan);
	if (unlikely(atomic_read(&chan->rx_thread.thread_done)))
		return -EPIPE;

	return 0;
}

static irqreturn_t hi3593_rx_irq(int irq, void *dev_id)
{
	struct hi3593_channel_priv *chan = dev_id;
	u64 now;

	netdev_vdbg(chan->ndev, "%s: enter\n", __func__);

	now = local_clock();
	if (chan->label_interval)
		chan->label_interval = (chan->label_interval + now - chan->last_irq_time) / 2;
	else if (chan->last_irq_time)
		chan->label_interval = now - chan->last_irq_time;

	chan->last_irq_time = now;

	atomic_set(&chan->rx_thread.rx, 1);

	wake_up_interruptible(&chan->rx_thread.wait);

	return IRQ_HANDLED;
}

static irqreturn_t hi3593_flag_irq(int irq, void *dev_id)
{
	struct hi3593_channel_priv *chan = dev_id;

	netdev_vdbg(chan->ndev, "%s: enter\n", __func__);

	disable_irq_nosync(irq);
	chan->flag_irq_disabled = true;

	mod_timer(&chan->rx_thread.irq_timeout,
		  jiffies + nsecs_to_jiffies(RX_FIFO_SIZE * chan->label_interval));

	atomic_set(&chan->rx_thread.rx, 1);

	wake_up_interruptible(&chan->rx_thread.wait);

	return IRQ_HANDLED;
}

static irqreturn_t hi3593_tx_full_irq(int irq, void *dev_id)
{
	struct hi3593_channel_priv *chan = dev_id;
	struct hi3593_priv *adev = chan->adev;

	dev_vdbg(&chan->adev->spi->dev, "%s: enter\n", __func__);

	atomic_set(&chan->fifo_full, 1);

	netif_stop_queue(chan->ndev);

	disable_irq_nosync(irq);

	enable_irq(gpio_to_irq(adev->tx_empty));

	return IRQ_HANDLED;
}

static irqreturn_t hi3593_tx_empty_irq(int irq, void *dev_id)
{
	struct hi3593_channel_priv *chan = dev_id;
	struct hi3593_priv *adev = chan->adev;

	dev_vdbg(&chan->adev->spi->dev, "%s: enter\n", __func__);

	disable_irq_nosync(irq);

	atomic_set(&chan->fifo_full, 0);

	enable_irq(gpio_to_irq(adev->tx_full));

	/* do not accept new transmit requests until all buffers are sent */
	if (skb_queue_empty(&chan->adev->txq))
		netif_wake_queue(chan->ndev);
	else
		schedule_work(&chan->work);

	return IRQ_HANDLED;
}

/* A little helper to prepare label filters bitmap for HW */
static inline void hi3593_reverse_bytes(u8 *dst,
		const u8 *src, unsigned int n)
{
	unsigned int i;
	src += n - 1;
	for (i = 0; i < n; i++)
		*dst++ = *src--;
}

static int
hi3593_configure_label_filters(struct hi3593_priv *adev,
		enum channel_type channel, const unsigned long *filters)
{
	u8 reg[NUM_RECEIVERS] = {CMD_WRITE_R1_LABELS, CMD_WRITE_R2_LABELS};
	u8 data_buf[NUM_LABELS / BITS_PER_BYTE];
	struct spi_transfer cmd = {0};
	struct spi_transfer data = {0};
	struct spi_message m;

	cmd.tx_buf = &reg[channel];
	cmd.len = 1;

	/* HW takes filter bitmap in reversed byte order, starting from label 0377 */
	hi3593_reverse_bytes(data_buf, (const u8*)filters, sizeof(data_buf));

	data.tx_buf = data_buf;
	data.len = sizeof(data_buf);

	spi_message_init(&m);
	spi_message_add_tail(&cmd, &m);
	spi_message_add_tail(&data, &m);
	return spi_sync(adev->spi, &m);
}

static int hi3593_hw_default_config(struct hi3593_priv *adev)
{
	u8 tx_buf[2] = {CMD_WRITE_FLAG, DEFAULT_FLAG_IAR_VALUE};
	int ret;

	ret = hi3593_master_reset(adev);
	if (ret)
		return ret;

	/* receiver flags and interrupt settings */
	ret = spi_write(adev->spi, tx_buf, sizeof(tx_buf));
	if (ret)
		return ret;
	adev->flag_iar = DEFAULT_FLAG_IAR_VALUE;
	return ret;
}

static int hi3593_channel_default_config(
			struct hi3593_channel_priv *chan)
{
	u8 val;

	/* as master and soft reset are performed for a whole chip state, it is
	 * needed to bring receiver or transmitter to default state, i.e. set
	 * default value in control register. Do not bother to clean label filters
	 * bitmap or priority matching labels on receivers, as they are used only
	 * if corresponding bit is enabled in control reg */

	val = hi3593_is_transmitter(chan->type) ?
			DEFAULT_TX_CR_VALUE : DEFAULT_RX_CR_VALUE;
	return __hi3593_write_ctrl_reg(chan->adev, chan->type, val);
}

static int hi3593_rx_thread_func(void *data)
{
	struct hi3593_channel_priv *chan = (struct hi3593_channel_priv*)data;
	unsigned long flags;

	while (true) {
		wait_event_interruptible(chan->rx_thread.wait,
			atomic_read(&chan->rx_thread.rx) ||
			atomic_read(&chan->rx_thread.thread_done));

		if (unlikely(atomic_read(&chan->rx_thread.thread_done)))
			break;

		if (unlikely(hi3593_receive(chan) < 0))
			break;

		spin_lock_irqsave(&chan->rx_thread.lock, flags);

		if (chan->flag_irq_disabled) {
			enable_irq(gpio_to_irq(chan->adev->flag_int[chan->type]));
			chan->flag_irq_disabled = false;
		}

		atomic_set(&chan->rx_thread.rx, 0);
		spin_unlock_irqrestore(&chan->rx_thread.lock, flags);
	}

	complete_and_exit(&chan->rx_thread.completion, 0);
	return 0;
}

static void hi3593_irq_timeout(struct timer_list *t)
{
	struct hi3593_channel_priv *chan =
			from_timer(chan, t, rx_thread.irq_timeout);
	unsigned long flags;

	netdev_vdbg(chan->ndev, "%s: enter\n", __func__);

	spin_lock_irqsave(&chan->rx_thread.lock, flags);

	if (chan->rx_irq_disabled) {
		enable_irq(gpio_to_irq(chan->adev->rx_int[chan->type]));
		chan->rx_irq_disabled = false;
	}

	spin_unlock_irqrestore(&chan->rx_thread.lock, flags);
}

/* prepare and start dedicated RX thread */
static int hi3593_init_rx_thread(struct hi3593_channel_priv *chan)
{
	atomic_set(&chan->rx_thread.thread_done, 0);
	atomic_set(&chan->rx_thread.rx, 0);

	init_waitqueue_head(&chan->rx_thread.wait);
	init_completion(&chan->rx_thread.completion);

	chan->rx_thread.task = kthread_run(hi3593_rx_thread_func,
					chan, "hi3593rx-%d", chan->type);
	if (IS_ERR(chan->rx_thread.task))
		return (int)PTR_ERR(chan->rx_thread.task);

	spin_lock_init(&chan->rx_thread.lock);
	timer_setup(&chan->rx_thread.irq_timeout, hi3593_irq_timeout, 0);

	return 0;
}

static void hi3593_stop_thread(struct hi3593_channel_priv *chan)
{
	atomic_set(&chan->rx_thread.thread_done, 1);
	wake_up_interruptible(&chan->rx_thread.wait);

	wait_for_completion(&chan->rx_thread.completion);

	del_timer_sync(&chan->rx_thread.irq_timeout);
}

static int hi3593_open(struct net_device *ndev)
{
	struct hi3593_channel_priv *chan = netdev_priv(ndev);
	struct hi3593_priv *adev = chan->adev;
	struct device *dev = &adev->spi->dev;
	int ret;

	netdev_dbg(ndev, "%s: enter\n", __func__);

	/* common open */
	ret = open_arinc429dev(ndev);
	if (ret)
		goto err;

	mutex_lock(&adev->dev_lock);

	/* perform intial setup on first open */
	if (!adev->active_channels) {
		ret = hi3593_hw_default_config(adev);
		if (ret)
			goto err_unlock;
	}

	hi3593_channel_default_config(chan);

	switch (chan->type) {
	case RECEIVER_1:
	case RECEIVER_2:
		if (!gpio_is_valid(adev->rx_int[chan->type])) {
			netdev_err(ndev,
				"GPIO line for receiver %d INT IRQ is invalid\n",
				(int)chan->type + 1);
			ret = -EINVAL;
			goto err_unlock;
		}
		if (!gpio_is_valid(adev->flag_int[chan->type])) {
			netdev_err(ndev,
				"GPIO line for receiver %d FLAG IRQ is invalid\n",
				(int)chan->type + 1);
			ret = -EINVAL;
			goto err_unlock;
		}

		netdev_dbg(ndev, "%s: GPIO IRQ ok\n", __func__);

		ret = hi3593_init_rx_thread(chan);

		ret = devm_request_irq(dev,
			gpio_to_irq(adev->rx_int[chan->type]),
			hi3593_rx_irq,
			IRQF_TRIGGER_RISING,
			adev->labels[chan->type],
			chan);
		if (ret) {
			netdev_err(ndev,
				"failed to acquire receiver %d INT interrupt\n",
				(int)chan->type + 1);

			hi3593_stop_thread(chan);

			goto err_unlock;
		}
		ret = devm_request_irq(dev,
			gpio_to_irq(adev->flag_int[chan->type]),
			hi3593_flag_irq,
			IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
			adev->labels[chan->type],
			chan);
		if (ret) {
			netdev_err(ndev,
				"failed to acquire receiver %d FLAG interrupt\n",
				(int)chan->type + 1);

			hi3593_stop_thread(chan);

			goto err_unlock;
		}

		break;

	case TRANSMITTER:
		if (!gpio_is_valid(adev->tx_full) ||
			!gpio_is_valid(adev->tx_empty)) {
			netdev_err(ndev, "GPIO lines for TX IRQs are invalid\n");
			ret = -EINVAL;
			goto err_unlock;
		}

		ret = devm_request_threaded_irq(dev,
			gpio_to_irq(adev->tx_full),
			NULL,
			hi3593_tx_full_irq,
			IRQF_ONESHOT | IRQF_TRIGGER_RISING,
			DRV_NAME "-tx-full",
			chan);
		if (ret) {
			netdev_err(ndev, "failed to acquire tx-full\n");
			goto err_unlock;
		}

		ret = devm_request_threaded_irq(dev,
			gpio_to_irq(adev->tx_empty),
			NULL,
			hi3593_tx_empty_irq,
			IRQF_ONESHOT | IRQF_TRIGGER_RISING,
			DRV_NAME "-tx-empty",
			chan);
		if (ret) {
			netdev_err(ndev, "failed to acquire tx-empty\n");
			goto err_unlock;
		}

		/* start with TX empty interrupt disabled.
		 * In case TX FIFO becomes full this irq will be switched on
		 * and flip off TX full. */
		disable_irq(gpio_to_irq(adev->tx_empty));

		INIT_WORK(&chan->work, hi3593_tx_work_handler);
		break;

	default:
		BUG();
		break;
	}

	adev->active_channels++;

	mutex_unlock(&adev->dev_lock);

	netif_start_queue(ndev);

	return 0;

err_unlock:
	mutex_unlock(&chan->adev->dev_lock);

err:
	return ret;
}

static int hi3593_close(struct net_device *ndev)
{
	struct hi3593_channel_priv *chan = netdev_priv(ndev);
	struct hi3593_priv *adev = chan->adev;
	struct device *dev = &adev->spi->dev;

	netdev_dbg(ndev, "%s: enter\n", __func__);

	mutex_lock(&adev->dev_lock);

	switch (chan->type) {
	case RECEIVER_1:
	case RECEIVER_2:
		hi3593_stop_thread(chan);

		devm_free_irq(dev, gpio_to_irq(adev->rx_int[chan->type]), chan);
		devm_free_irq(dev, gpio_to_irq(adev->flag_int[chan->type]), chan);

		break;

	case TRANSMITTER:
		netif_stop_queue(ndev);

		flush_work(&chan->work);

		skb_queue_purge(&adev->txq);

		devm_free_irq(dev, gpio_to_irq(adev->tx_full), chan);
		devm_free_irq(dev, gpio_to_irq(adev->tx_empty), chan);

		break;

	default:
		BUG();
		break;
	}

	close_arinc429dev(ndev);

	adev->active_channels--;

	mutex_unlock(&adev->dev_lock);

	return 0;
}

static int hi3593_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	int ret = NETDEV_TX_OK;

	netdev_vdbg(dev, "%s: enter\n", __func__);

	/* discard transmit frame if this is RX channel */
	if (chan->type != TRANSMITTER) {
		netdev_dbg(dev,
			"%s: attempted to transmit on RX channel\n", __func__);

		dev->stats.tx_dropped++;

		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (arinc429_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	skb_queue_tail(&adev->txq, skb);

	/* keep amount of queued messages almost equal to TX FIFO size */
	if (atomic_inc_return(&adev->txq_count) >= TX_FIFO_SIZE)
		netif_stop_queue(chan->ndev);

	schedule_work(&chan->work);

	return ret;
}

static const struct net_device_ops hi3593_netdev_ops = {
	.ndo_open               = hi3593_open,
	.ndo_stop               = hi3593_close,
	.ndo_start_xmit         = hi3593_start_xmit,
};

/* sysfs properties */

static ssize_t
hi3593_flip_label_bits_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;

	return sprintf(buf, "%s\n",
		(adev->ctrl_regs[chan->type] & label_flip_bits[chan->type]) ?
		"on" : "off");
}

static ssize_t
hi3593_flip_label_bits_set(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	int ret = -EINVAL;

	if (!strncmp(buf, "on", 2)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_set_ctrl_reg_bits(adev,
				chan->type, label_flip_bits[chan->type]);
		mutex_unlock(&adev->dev_lock);
	} else if (!strncmp(buf, "off", 3)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_clear_ctrl_reg_bits(adev,
				chan->type, label_flip_bits[chan->type]);
		mutex_unlock(&adev->dev_lock);
	}

	return ret < 0 ? ret : count;
}

static DEVICE_ATTR(flip_label_bits, S_IRUGO | S_IWUSR,
	hi3593_flip_label_bits_get, hi3593_flip_label_bits_set);


static ssize_t
hi3593_rate_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);

	return sprintf(buf, "%s\n", (chan->rate == RATE_HIGH) ? "high" : "low");
}

static ssize_t
hi3593_rate_set(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	int ret = -EINVAL;

	if (!strncmp(buf, "low", 3)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_set_ctrl_reg_bits(adev,
				chan->type, rate_bits[chan->type]);
		if (!ret)
			chan->rate = RATE_LOW;
		mutex_unlock(&adev->dev_lock);
	} else if (!strncmp(buf, "high", 4)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_clear_ctrl_reg_bits(adev,
				chan->type, rate_bits[chan->type]);
		if (!ret)
			chan->rate = RATE_HIGH;
		mutex_unlock(&adev->dev_lock);
	}
	return ret < 0 ? ret : count;
}
static DEVICE_ATTR(rate, S_IRUGO | S_IWUSR,
	hi3593_rate_get, hi3593_rate_set);

static ssize_t
hi3593_parity_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;

	return sprintf(buf, "%s\n",
		(adev->ctrl_regs[chan->type] & parity_bits[chan->type]) ?
		"on" : "off");
}

static ssize_t
hi3593_parity_set(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	int ret = -EINVAL;

	if (!strncmp(buf, "on", 2)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_set_ctrl_reg_bits(adev,
				chan->type, parity_bits[chan->type]);
		mutex_unlock(&adev->dev_lock);
	} else if (!strncmp(buf, "off", 3)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_clear_ctrl_reg_bits(adev,
				chan->type, parity_bits[chan->type]);
		mutex_unlock(&adev->dev_lock);
	}

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR(parity, S_IRUGO | S_IWUSR,
	hi3593_parity_get, hi3593_parity_set);

static ssize_t
hi3593_label_filter_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;

	if (hi3593_is_transmitter(chan->type))
		return sprintf(buf, "N/A\n");

	return sprintf(buf, "%s\n",
		(adev->ctrl_regs[chan->type] & RECEIVER_LABEL_FILTERS_ENABLED) ?
			"on" : "off");
}

static ssize_t
hi3593_label_filter_set(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	int ret = -EINVAL;

	if (hi3593_is_transmitter(chan->type))
		return ret;

	if (!strncmp(buf, "on", 2)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_set_ctrl_reg_bits(adev,
			chan->type, RECEIVER_LABEL_FILTERS_ENABLED);
		mutex_unlock(&adev->dev_lock);
	} else if (!strncmp(buf, "off", 3)) {
		mutex_lock(&adev->dev_lock);
		ret = __hi3593_clear_ctrl_reg_bits(adev,
			chan->type, RECEIVER_LABEL_FILTERS_ENABLED);
		mutex_unlock(&adev->dev_lock);
	}

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR(label_filter, S_IRUGO | S_IWUSR,
	hi3593_label_filter_get, hi3593_label_filter_set);

static ssize_t
hi3593_label_filter_bitmap_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	const unsigned long *p[NUM_RECEIVERS] = {
		adev->r1_label_filter, adev->r2_label_filter};

	if (hi3593_is_transmitter(chan->type))
		return sprintf(buf, "N/A\n");

	return bitmap_print_to_pagebuf(true, buf, p[chan->type], NUM_LABELS);
}

static ssize_t
hi3593_label_filter_bitmap_set(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	unsigned long *p[NUM_RECEIVERS] = {
		adev->r1_label_filter, adev->r2_label_filter};
	DECLARE_BITMAP(bits, NUM_LABELS);
	int ret = -EINVAL;

	if (hi3593_is_transmitter(chan->type))
		return ret;

	ret = bitmap_parselist(buf, bits, NUM_LABELS);
	if (ret)
		return ret;

	mutex_lock(&adev->dev_lock);

	ret = hi3593_configure_label_filters(adev, chan->type, bits);
	if (!ret)
		/* update cached copy */
		bitmap_copy(p[chan->type], bits, NUM_LABELS);

	mutex_unlock(&adev->dev_lock);

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR(label_filter_bitmap, S_IRUGO | S_IWUSR,
	hi3593_label_filter_bitmap_get, hi3593_label_filter_bitmap_set);

static ssize_t
hi3593_tx_odd_even_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;

	if (hi3593_is_receiver(chan->type))
		return sprintf(buf, "N/A\n");

	return sprintf(buf, "%s\n",
			(adev->ctrl_regs[TRANSMITTER] & TRANSMITTER_ODD_EVEN) ?
				"even" : "odd");
}

static ssize_t
hi3593_tx_odd_even_set(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	struct hi3593_priv *adev = chan->adev;
	int ret = -EINVAL;

	if (hi3593_is_receiver(chan->type))
		return ret;

	if (!strncmp(buf, "even", 4)) {
		mutex_lock(&adev->dev_lock);

		ret = __hi3593_set_ctrl_reg_bits(adev,
				TRANSMITTER, TRANSMITTER_ODD_EVEN);

		mutex_unlock(&adev->dev_lock);
	} else if (!strncmp(buf, "odd", 3)) {
		mutex_lock(&adev->dev_lock);

		ret = __hi3593_clear_ctrl_reg_bits(adev,
				TRANSMITTER, TRANSMITTER_ODD_EVEN);

		mutex_unlock(&adev->dev_lock);
	}

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR(tx_odd_even, S_IRUGO | S_IWUSR,
	hi3593_tx_odd_even_get, hi3593_tx_odd_even_set);

static ssize_t
hi3593_channel_type_get(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct hi3593_channel_priv *chan = netdev_priv(dev);
	return sprintf(buf, "%s\n", channel_type_names[chan->type]);
}

static DEVICE_ATTR(channel_type, S_IRUSR,
	hi3593_channel_type_get, NULL);

static struct attribute *hi3593_sysfs_entries[] = {
	&dev_attr_flip_label_bits.attr,
	&dev_attr_rate.attr,
	&dev_attr_parity.attr,
	&dev_attr_label_filter.attr,
	&dev_attr_label_filter_bitmap.attr,
	&dev_attr_tx_odd_even.attr,
	&dev_attr_channel_type.attr,
	NULL
};

static struct attribute_group hi3593_attribute_group = {
	.name = "arinc429",
	.attrs = hi3593_sysfs_entries,
};

static struct hi3593_channel_priv*
hi3593_alloc_channel(struct hi3593_priv *adev, enum channel_type type)
{
	struct net_device *dev;
	struct hi3593_channel_priv *chan;
	int ret;

	dev_dbg(&adev->spi->dev, "%s: enter\n", __func__);

	dev = alloc_arinc429dev(sizeof(*chan), adev->labels[type], 1);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	chan = netdev_priv(dev);

	chan->adev = adev;
	chan->ndev = dev;
	chan->type = type;

	/* FIXME: clock freq is not used in ARINC protocol driver
	in CAN framework it was used to calculate bit timing. In ARINC theres is
	only RATE (low-speed and high-speed) */
	chan->arinc429.clock.freq = chan->adev->freq;

	/* TODO: add more callbacks into ARINC framework to manage:
	 - rate
	 - label filters
	 - priority matching
	 - parity
	 - label bits flip

	 As an alternative to callbacks - use IOCTLs on dev/sock (?)
	 */

	/* init sysfs attributes */
	dev->sysfs_groups[0] = &hi3593_attribute_group;

	SET_NETDEV_DEV(dev, &adev->spi->dev);
	dev->netdev_ops = &hi3593_netdev_ops;

	ret = register_arinc429dev(dev);
	if (ret) {
		free_arinc429dev(dev);
		dev_err(&adev->spi->dev, "registering failed (ret=%d)\n", ret);
		return ERR_PTR(ret);
	}

	dev_info(&adev->spi->dev, "device registered\n");

	return chan;
}

static void hi3593_free_channel(struct hi3593_channel_priv *chan)
{
	if (!IS_ERR_OR_NULL(chan)) {
		unregister_arinc429dev(chan->ndev);
		free_arinc429dev(chan->ndev);
	}
}

/* Parses Device Tree data for the chip.
 * Supported properties:
 *   arinc-frequency - ARINC429 clock frequency, (1,2,4,6..30 MHz)
 *   master_reset-gpio - external HW reset pin
 *   rx-chan@<idx> {
 *     label - interface name to assign
 *     rx_int-gpio - Receiver <idx> interrupt (RxINT)
 *   }
 *   tx-chan@<idx> {
 *     label - interface name to assign
 *     tx_full-gpio - Transmitter FIFO full interrupt (TFULL)
 *     tx_empty-gpio - Transmitter FIFO empty (TFEMPTY)
 *   }
 */
static int hi3593_probe_dt(struct hi3593_priv *adev)
{
	struct device *dev = &adev->spi->dev;
	struct device_node *node = dev->of_node;
	struct device_node *chan;
	int ret;

	if (!node) {
		dev_err(dev, "Device does not have associated DT data\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "arinc-frequency", &adev->freq);
	if (ret < 0) {
		if (ret == -EINVAL) {
			adev->freq = DEFAULT_ARINC429_CLK_FREQ;
		} else {
			dev_err(dev,
				"Failed to get arinc-frequency node, err: %d\n",
				ret);
			return ret;
		}
	}
	dev_dbg(dev, "selected ARINC429 clock frequency: %u\n", adev->freq);

	adev->hw_reset = of_get_named_gpio(node, "master_reset-gpio", 0);
	if (gpio_is_valid(adev->hw_reset))
		if (devm_gpio_request_one(dev, adev->hw_reset,
				GPIOF_OUT_INIT_LOW, "HI-3593 Reset"))
			adev->hw_reset = -EINVAL;

	/* Receiver 1 */
	chan = of_get_child_by_name(node, "rx1-chan");
	if (!chan) {
		dev_err(dev, "Failed to get rx1-chan node\n");
		return -EINVAL;
	}
	adev->labels[RECEIVER_1] =
		of_get_property(chan, "label", NULL) ? : "arinc429rx%d";

	adev->rx_int[RECEIVER_1] = of_get_named_gpio(chan, "int-gpio", 0);
	if (!gpio_is_valid(adev->rx_int[RECEIVER_1]) ||
		devm_gpio_request_one(dev, adev->rx_int[RECEIVER_1],
				GPIOF_IN, "HI-3593 RX1 INT pin")) {
		dev_err(dev, "Failed to get RX IRQ GPIO\n");
		return -EINVAL;
	}
	adev->flag_int[RECEIVER_1] = of_get_named_gpio(chan, "flag-gpio", 0);
	if (!gpio_is_valid(adev->flag_int[RECEIVER_1]) ||
		devm_gpio_request_one(dev, adev->flag_int[RECEIVER_1],
				GPIOF_IN, "HI-3593 RX1 FLAG pin")) {
		dev_err(dev, "Failed to get FLAG IRQ GPIO\n");
		return -EINVAL;
	}

	/* Receiver 2 */
	chan = of_get_child_by_name(node, "rx2-chan");
	if (!chan) {
		dev_err(dev, "Failed to get rx2-chan node\n");
		return -EINVAL;
	}
	adev->labels[RECEIVER_2] =
		of_get_property(chan, "label", NULL) ? : "arinc429rx%d";

	adev->rx_int[RECEIVER_2] = of_get_named_gpio(chan, "int-gpio", 0);
	if (!gpio_is_valid(adev->rx_int[RECEIVER_2]) ||
		devm_gpio_request_one(dev, adev->rx_int[RECEIVER_2],
				GPIOF_IN, "HI-3593 RX2 INT pin")) {
		dev_err(dev, "Failed to get RX IRQ GPIO\n");
		return -EINVAL;
	}
	adev->flag_int[RECEIVER_2] = of_get_named_gpio(chan, "flag-gpio", 0);
	if (!gpio_is_valid(adev->flag_int[RECEIVER_2]) ||
		devm_gpio_request_one(dev, adev->flag_int[RECEIVER_2],
				GPIOF_IN, "HI-3593 RX2 FLAG pin")) {
		dev_err(dev, "Failed to get RX IRQ GPIO\n");
		return -EINVAL;
	}

	/* Transmitter */
	chan = of_get_child_by_name(node, "tx-chan");
	if (!chan) {
		dev_err(dev, "Failed to get tx-chan node\n");
		return -EINVAL;
	}
	adev->labels[TRANSMITTER] =
		of_get_property(chan, "label", NULL) ? : "arinc429tx%d";

	adev->tx_full = of_get_named_gpio(chan, "full-gpio", 0);
	if (!gpio_is_valid(adev->tx_full) ||
		devm_gpio_request_one(dev, adev->tx_full,
				GPIOF_IN, "HI-3593 TX FIFO full")) {
			adev->tx_full = -EINVAL;
		dev_err(dev, "Failed to get TX FULL IRQ GPIO\n");
		return -EINVAL;
	}

	adev->tx_empty = of_get_named_gpio(chan, "empty-gpio", 0);
	if (!gpio_is_valid(adev->tx_empty) ||
		devm_gpio_request_one(dev, adev->tx_empty,
				GPIOF_IN, "HI-3593 TX FIFO empty")) {
			adev->tx_empty = -EINVAL;
		dev_err(dev, "Failed to get TX EMPTY IRQ GPIO\n");
		return -EINVAL;
	}

	return 0;
}

/* Executes built-in self test procedure */
static int hi3593_self_test(struct hi3593_priv *adev)
{
	u8 tx_buf[5] = { CMD_WRITE_MSG, 0x12, 0x34, 0x56, 0x78 };
	u8 rx_buf[4] = {0};
	u8 r2_reply[4] = {0xed, 0xcb, 0xa9, 0x87};
	u8 old_regs[NUM_CHANNELS];
	u8 plm_cmd[4] = {CMD_WRITE_R1_PLMR, 0, 0, 0x78};
	u8 cmd;
	int ret;

	/* backup current reg values */
	memcpy(old_regs, adev->ctrl_regs, NUM_CHANNELS);

	dev_dbg(&adev->spi->dev, "%s: enter\n", __func__);

	/* enable internal loopback */
	ret = __hi3593_write_ctrl_reg(adev, TRANSMITTER,
			TRANSMITTER_MODE | TRANSMITTER_SELF_TEST);
	if (ret)
		return ret;

	dev_dbg(&adev->spi->dev, "%s: self test enabled\n", __func__);

	/* if ARINC429 bus has active transmitter connected to our RX channels
	 * FIFO will be filled by this data before we got our self test packet.
	 * set priority matching and our test labels for R1 and R2, perform
	 * soft reset to clear FIFO and execute self test. */
	__hi3593_write_ctrl_reg(adev, RECEIVER_1, RECEIVER_PRIORITY_MATCH_ON);
	__hi3593_write_ctrl_reg(adev, RECEIVER_2, RECEIVER_PRIORITY_MATCH_ON);

	/* set Receiver 1 match labels */
	spi_write(adev->spi, plm_cmd, sizeof(plm_cmd));

	/* set Receiver 2 match labels */
	plm_cmd[0] = CMD_WRITE_R2_PLMR;
	plm_cmd[3] = 0x87;
	spi_write(adev->spi, plm_cmd, sizeof(plm_cmd));

	cmd = CMD_SOFT_RESET;
	spi_write(adev->spi, &cmd, 1);

	/* If Transmit Control Register bit SELFTEST is equal '1', the
	 * transmitter serial output data is internally looped-back into the
	 * receiver 1. The data will appear inverted (compliment) on receiver 2.
	 * Data passes unmodified from transmitter to receiver 1. Setting
	 * Transmit Control register bit SELFTEST to '1' forces TXAOUT and
	 * TXBOUT to the Null state to prevent self-test data from appearing on
	 * theARINC 429 bus. */

	ret = spi_write(adev->spi, tx_buf, sizeof(tx_buf));
	if (ret)
		return ret;

	dev_dbg(&adev->spi->dev, "%s: TX data: %#x %02x %02x %02x\n",
		__func__, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);

	/* let HW settle. Experimentally found that bits appear on RX in ~220usec */
	udelay(300);

	/* read back first receiver and compare with source data */
	cmd = CMD_READ_R1_MSG;
	ret = spi_write_then_read(adev->spi, &cmd, 1, rx_buf, 4);
	if (ret)
		return ret;

	dev_dbg(&adev->spi->dev, "%s: R1 data: %#x %02x %02x %02x\n",
		__func__, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
	if (memcmp(&tx_buf[1], rx_buf, 4))
		return -EINVAL;

	cmd = CMD_READ_R2_MSG;
	ret = spi_write_then_read(adev->spi, &cmd, 1, rx_buf, 4);
	if (ret)
		return ret;
	dev_dbg(&adev->spi->dev, "%s: R2 data: %#x %02x %02x %02x\n",
		__func__, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);

	if (memcmp(r2_reply, rx_buf, 4))
		return -EINVAL;

	/* restore control regs values */
	memcpy(adev->ctrl_regs, old_regs, NUM_CHANNELS);

	dev_dbg(&adev->spi->dev, "%s: self test completed\n", __func__);
	return 0;
}


static int hi3593_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct hi3593_priv *adev;
	int ret;
	enum channel_type ch;

	dev_dbg(dev, "%s: enter\n", __func__);

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	mutex_init(&adev->dev_lock);

	adev->spi = spi;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	dev_set_drvdata(dev, adev);

	ret = hi3593_probe_dt(adev);
	if (ret)
		return ret;

	adev->spi_buffer = kmalloc(SPI_RX_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);

	if (adev->freq != DEFAULT_ARINC429_CLK_FREQ)
		if (hi3593_set_clock(adev->spi, adev->freq)) {
			dev_err(dev, "Unable to set freq: %d\n", adev->freq);
			return -EINVAL;
		}

	/* There is no ID regs on this chip and we can't use status registers
	 *  as well. If data flows on ARINC429, their state is changed from default.
	 * Let's do chip verification by executng self-test.
	 * Do not bother to clean-up FIFOs after test,
	 * on first network device open call HW will be reset */
	if (hi3593_self_test(adev))
		return -ENODEV;

	for (ch = FIRST_CHANNEL; ch < NUM_CHANNELS; ch++) {
		adev->channels[ch] = hi3593_alloc_channel(adev, ch);
		if (IS_ERR(adev->channels[ch])) {
			ret = PTR_ERR(adev->channels[ch]);
			goto cleanup;
		}
	}

	skb_queue_head_init(&adev->txq);

	hi3593_register_debugfs(adev);

	return 0;

cleanup:
	for (ch = FIRST_CHANNEL; ch < NUM_CHANNELS; ch++)
		hi3593_free_channel(adev->channels[ch]);

	return ret;
}

static int hi3593_spi_remove(struct spi_device *spi)
{
	struct hi3593_priv *adev = dev_get_drvdata(&spi->dev);
	enum channel_type ch;

	dev_dbg(&spi->dev, "%s: enter\n", __func__);

	for (ch = FIRST_CHANNEL; ch < NUM_CHANNELS; ch++)
		hi3593_free_channel(adev->channels[ch]);

	hi3593_unregister_debugfs(adev);

	return 0;
}

static const struct spi_device_id hi3593_id[] = {
	{ DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, hi3593_id);

static struct spi_driver hi3593_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = hi3593_spi_probe,
	.remove = hi3593_spi_remove,
	.id_table = hi3593_id,
};
module_spi_driver(hi3593_driver);

MODULE_AUTHOR("Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>");
MODULE_DESCRIPTION("HoltIC HI-3593 ARINC429 driver");
MODULE_LICENSE("GPL v2");

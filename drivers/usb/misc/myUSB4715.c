// original USB module headers
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
// i2c adapter included headers
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/of.h>

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jamies Genesys Electronics Design");
MODULE_DESCRIPTION("A driver for USB4715 on eBet eGDU");

#define ADAPTER_NAME "eGDU_I2C_ADAPTER"

#define VENDOR_ID 0x0424
#define PRODUCT_ID 0x494c

#define CTL_REQUEST_TYPE__VSM_DEVICE_RQST 0x40
#define CTL_REQUEST_TYPE__VSM_INTERFACE_RQST 0x41
#define CTL_REQUEST_TYPE__VSM_DEVICE_READ 0xC0
#define CTL_REQUEST_TYPE__VSM_INTERFACE_READ 0xC1

#define I2C_READ_BIT 0x01
#define I2C_READ_CMD 0x72
#define I2C_WRITE_CMD 0x71

#define SEND_NACK 0x04
#define SEND_START 0x02
#define SEND_STOP 0x01

#define I2C_ENTER_PASSTHRG 0x70
#define I2C_EXIT_PASSTHRG 0x73

struct usb_i2c_dev
{
	struct usb_device *usb_dev;
	struct i2c_adapter adapter;
};

static int USB_I2C_Transfer(struct i2c_adapter *adap, bool isRead, uint8_t byAddr,
							uint8_t *pbyBuffer, uint16_t wLength, uint32_t *wdActualLength);
static int USB_I2C_Transfer(struct i2c_adapter *adap, bool isRead, uint8_t byAddr,
							uint8_t *pbyBuffer, uint16_t wLength, uint32_t *wdActualLength)
{
	struct usb_i2c_dev *dev = (struct usb_i2c_dev *)adap->algo_data;
	struct usb_device *usb_dev = dev->usb_dev;

	uint8_t byFlags, byCmd;
	int bRet = 0;

	uint16_t wAddress, temp = 0;

	if (wLength > 512)
	{
		return -1;
	}

	byAddr = byAddr << 1;

	if (isRead) // I2c Read
	{
		byFlags = SEND_NACK | SEND_START | SEND_STOP;

		byCmd = I2C_READ_CMD;
		byAddr |= I2C_READ_BIT;
		temp = byAddr;
		temp |= ((uint16_t)byFlags) << 8;

		wAddress = temp;
		bRet = usb_control_msg_recv(usb_dev, usb_rcvctrlpipe(usb_dev, 0), I2C_READ_CMD, CTL_REQUEST_TYPE__VSM_INTERFACE_READ, wAddress, 0, &pbyBuffer[0], wLength, 500, GFP_KERNEL);

		// usb_control_msg_recv returns 0 or error
		*wdActualLength = wLength;
	}
	else
	{
		byFlags = SEND_START | SEND_STOP;

		byCmd = I2C_WRITE_CMD;
		temp = byAddr;
		temp |= ((uint16_t)byFlags) << 8;

		wAddress = temp;

		bRet = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0), I2C_WRITE_CMD, CTL_REQUEST_TYPE__VSM_INTERFACE_RQST, wAddress, 0, &pbyBuffer[0], wLength, 500);

		// usb_control_msg returns number of bytes or error
		*wdActualLength = bRet;
	}

	if (bRet < 0)
	{
		dev_err(&adap->dev,
				"ret %d. isRead %d byAddr 0x%02X wLength %d wdActualLength %d\n",
				bRet, isRead, byAddr, wLength, *wdActualLength);
		return bRet;
	}
	else if (*wdActualLength != wLength)
	{
		dev_err(&adap->dev,
				"write not Completed. ret %d isRead %d byAddr 0x%02X wLength %d wdActualLength %d\n",
				bRet, isRead, byAddr, wLength, *wdActualLength);
		return -1;
	}
	else
	{
		dev_dbg(&adap->dev,
				 "Success! isRead %d byAddr 0x%02X wLength %d wdActualLength %d\n",
				 isRead, byAddr, wLength, *wdActualLength);
		return 0;
	}

	return bRet;
}

/*
** This function used to get the functionalities that are supported
** by this bus driver.
*/
static u32 i2c_algo_func(struct i2c_adapter *adap)
{
	// Scope based on edt-ft5x06 and OPT3001 drivers.
	return (I2C_FUNC_I2C |
			I2C_FUNC_SMBUS_BYTE |
			I2C_FUNC_SMBUS_BYTE_DATA |
			I2C_FUNC_SMBUS_WORD_DATA |
			// I2C_FUNC_SMBUS_QUICK |
			// I2C_FUNC_SMBUS_PROC_CALL |
			// I2C_FUNC_SMBUS_WRITE_BLOCK_DATA |
			// I2C_FUNC_SMBUS_I2C_BLOCK |
			// I2C_FUNC_SMBUS_PEC|
			0);
}

/*
** This function will be called whenever you call I2C read/write APIs like
** i2c_master_send(), i2c_master_recv(), or i2c_transfer().
** It handles raw I2C transfers, corresponding to I2C_FUNC_I2C functionality.
**
** Parameters:
**   @adap: Pointer to the I2C adapter structure associated with this algorithm.
**   @msgs: Pointer to an array of i2c_msg structures, each representing one message
**          (read or write) to send over the bus.
**   @num:  The number of messages in the msgs array. Can be >1 for repeated start transfers.
**
** Return:
**   >= 0: Number of messages successfully transferred.
**   <  0: A negative error code on failure (e.g., -EIO, -ENXIO).
*/
static s32 i2c_algo_i2c_xfer(struct i2c_adapter *adap,
							 struct i2c_msg *msgs,
							 int num)
{
	pr_debug("my_usb_devdrv - In i2c_algo_i2c_xfer\n");

	int number_succes = 0;

	for (number_succes = 0; number_succes < num; number_succes++)
	{
		int ret;

		struct i2c_msg *msg = &msgs[number_succes];

		bool isRead;
		uint8_t byAddr = msg->addr;
		uint8_t *pbyBuffer = msg->buf;
		uint32_t wLength = msg->len;
		uint32_t wdActualLength;

		if (msg->flags & I2C_M_RD)
		{
			isRead = true;
		}
		else
		{
			isRead = false;
		}

		ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
	}

	return number_succes;
}

/*
 * Handles SMBus-level transfers for the associated I2C adap.
 *
 * This function is invoked by the I2C core when performing SMBus transfers
 * such as i2c_smbus_read_word_data(), i2c_smbus_write_word_data(), etc.
 * It is used to implement support for SMBus protocol operations over the
 * USB-to-I2C bridge.
 *
 * Parameters:
 *   @adap:        Pointer to the I2C adap structure.
 *   @addr:        7-bit SMBus/I2C slave address of the target device.
 *   @flags:       Flags controlling the SMBus transfer (typically 0).
 *   @read_write:  Direction of the transfer (I2C_SMBUS_READ or I2C_SMBUS_WRITE).
 *   @command:     Command or register address to send to the device.
 *   @size:        Size/type of SMBus transaction (e.g., I2C_SMBUS_WORD_DATA).
 *   @data:        Data to write or buffer to read into, depending on direction.
 *
 * Returns:
 *   0 on success, or a negative error code on failure (e.g., -EIO, -ENXIO).
 */
static s32 i2c_algo_smbus_xfer(struct i2c_adapter *adap,
							   u16 addr,
							   unsigned short flags,
							   char read_write,
							   u8 command,
							   int size,
							   union i2c_smbus_data *data)
{
	pr_debug("my_usb_devdrv - In i2c_algo_smbus_xfer. Size = %d\n", size);

	int ret;

	bool isRead;
	uint8_t byAddr;
	uint8_t pbyBuffer[I2C_SMBUS_BLOCK_MAX + 2];
	uint32_t wLength;
	uint32_t wdActualLength;

	// error checking
	if (addr > 0x7F)
	{
		dev_err(&adap->dev, "Address 0x%04X > 0x7F\n", addr);
		return -1;
	}

	byAddr = (uint8_t)addr;

	switch (size)
	{
	case I2C_SMBUS_BYTE: // I2C_FUNC_SMBUS_BYTE

	{
		if (read_write == I2C_SMBUS_WRITE)
		{
			// Write Command + Data together
			isRead = false;
			pbyBuffer[0] = command;
			wLength = 1;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
		}
		else
		{
			// Read Data
			isRead = true;
			wLength = 1;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
			memcpy(&data->byte, &pbyBuffer[0], 1);
		}
		break;
	}
	case I2C_SMBUS_BYTE_DATA: // I2C_FUNC_SMBUS_BYTE_DATA
	{
		if (read_write == I2C_SMBUS_WRITE)
		{
			// Write Command + Data together
			isRead = false;
			pbyBuffer[0] = command;
			memcpy(&pbyBuffer[1], &data->byte, 1);
			wLength = 2;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
		}
		else
		{
			// Write Command
			isRead = false;
			pbyBuffer[0] = command;
			wLength = 1;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;

			// Read Data
			isRead = true;
			wLength = 1;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
			memcpy(&data->byte, &pbyBuffer[0], 1);
		}
		break;
	}
	case I2C_SMBUS_WORD_DATA: // I2C_FUNC_SMBUS_WORD_DATA
	{
		if (read_write == I2C_SMBUS_WRITE)
		{
			// Write Command + Data together
			isRead = false;
			pbyBuffer[0] = command;
			memcpy(&pbyBuffer[1], &data->word, 2);
			wLength = 3;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
		}
		else
		{
			// Write Command
			isRead = false;
			pbyBuffer[0] = command;
			wLength = 1;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;

			// Read Data
			isRead = true;
			wLength = 2;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
			memcpy(&data->word, &pbyBuffer[0], 2);
		}
		break;
	}
	case I2C_SMBUS_I2C_BLOCK_DATA: // I2C_FUNC_SMBUS_I2C_BLOCK
	{
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
		{
			dev_err(&adap->dev, "Block write length %u > %u n", data->block[0], I2C_SMBUS_BLOCK_MAX);
			return -1;
		}

		if (read_write == I2C_SMBUS_WRITE)
		{
			// Write Command + Data together
			isRead = false;
			pbyBuffer[0] = command;
			wLength = data->block[0];
			memcpy(&pbyBuffer[1], &data->block[1], wLength);
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
		}
		else
		{
			// Write Command
			isRead = false;
			pbyBuffer[0] = command;
			wLength = 1;
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;

			// Read Data
			isRead = true;
			wLength = data->block[0];
			ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
			if (ret < 0)
				return ret;
			memcpy(&data->block[1], &pbyBuffer[0], wLength);
		}
		break;
	}
	// NOTE - this is not functional - I don't think it is needed for now
	case I2C_SMBUS_BLOCK_DATA: // I2C_FUNC_SMBUS_WRITE_BLOCK_DATA
	// {
	// 	if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
	// 	{
	// 		dev_err(&adap->dev, "Block write length %u > %u\n", data->block[0], I2C_SMBUS_BLOCK_MAX);
	// 		return -1;
	// 	}
	//
	// 	if (read_write == I2C_SMBUS_WRITE)
	// 	{
	// 		// Write Command + Data together
	// 		isRead = false;
	// 		pbyBuffer[0] = command;
	// 		wLength = data->block[0];
	// 		memcpy(&pbyBuffer[1], &data->block[1], wLength);
	// 		ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
	// 		if (ret < 0)
	// 			return ret;
	// 	}
	// 	else
	// 	{
	// 		// Write Command
	// 		isRead = false;
	// 		pbyBuffer[0] = command;
	// 		wLength = 1;
	// 		ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
	// 		if (ret < 0)
	// 			return ret;
	//
	// 		// Read Data
	// 		isRead = true;
	// 		wLength = I2C_SMBUS_BLOCK_MAX + 1;
	// 		ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
	// 		if (ret < 0)
	// 			return ret;
	// 		data->block[0] = pbyBuffer[0];
	// 		memcpy(&data->block[1], &pbyBuffer[1], data->block[0]);
	// 	}
	// 	break;
	// }

	// NOTE - this is not functional - I don't think it is needed for now
	case I2C_SMBUS_QUICK: // I2C_FUNC_SMBUS_QUICK
	// {
	// 	if (read_write == I2C_SMBUS_WRITE)
	// 	{
	// 		isRead = false;
	// 	}
	// 	else
	// 	{
	// 		isRead = true;
	// 	}
	// 	wLength = 0;
	// 	ret = USB_I2C_Transfer(adap, isRead, byAddr, pbyBuffer, wLength, &wdActualLength);
	// 	if (ret < 0)
	// 		return ret;
	// 	break;
	// }
	case I2C_SMBUS_PROC_CALL:
	case I2C_SMBUS_I2C_BLOCK_BROKEN:
	case I2C_SMBUS_BLOCK_PROC_CALL:
	/* These SMBus types are not yet supported by this adapter */
	default:
		return -EOPNOTSUPP; // Not supported
	}

	dev_dbg(&adap->dev, "Successfully completed %s\n", __func__);

	return 0;
}

/*
** I2C algorithm Structure
*/
static struct i2c_algorithm my_i2c_algorithm = {
	.smbus_xfer = i2c_algo_smbus_xfer,
	.master_xfer = i2c_algo_i2c_xfer,
	.functionality = i2c_algo_func,
};

static struct usb_device_id usb_dev_table[] = {
	{USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
	{},
};
MODULE_DEVICE_TABLE(usb, usb_dev_table);

static void RunTests(struct i2c_adapter *adap)
{
	static int test = 1;

	uint8_t testAddr = 0x44;
	// uint8_t testAddr = 0x40;
	struct i2c_client *client = i2c_new_dummy_device(adap, testAddr);
	if (IS_ERR(client))
	{
		dev_err(&adap->dev, "Failed to create dummy I2C device\n");
		return;
	}

	pr_debug("my_usb_devdrv - Running Test %u!\n\n\n\n", test);
	switch (test)
	{
	case 1:
	{ // test for I2C_FUNC_I2C
		struct i2c_msg msgs[2];
		u8 tx_buf[1] = {0x7E}; // Write 0x7E (man reg)
		u8 rx_buf[2] = {0};
		int ret;

		msgs[0].addr = testAddr; // Replace with your device's I2C address
		msgs[0].flags = 0;	 // Write
		msgs[0].len = sizeof(tx_buf);
		msgs[0].buf = tx_buf;

		msgs[1].addr = testAddr;
		msgs[1].flags = I2C_M_RD; // Read
		msgs[1].len = sizeof(rx_buf);
		msgs[1].buf = rx_buf;

		ret = i2c_transfer(adap, msgs, 2);
		if (ret < 0)
			dev_err(&adap->dev, "I2C transfer failed: %d\n", ret);
		else
			dev_dbg(&adap->dev, "I2C transfer success: Read 0x%02x%02x\n", rx_buf[0], rx_buf[1]);
	}


	break;
	case 2:
	{ // Test for I2C_FUNC_SMBUS_BYTE
		{
			int ret = i2c_smbus_write_byte(client, 0x7E);
			if (ret < 0)
				dev_err(&adap->dev, "I2C SMBus Byte Write failed: %d\n", ret);
			else
				dev_dbg(&adap->dev, "I2C SMBus Byte Write success\n");
		}
		{
			int ret = i2c_smbus_read_byte(client);
			if (ret < 0)
				dev_err(&adap->dev, "I2C SMBus Byte Read failed: %d\n", ret);
			else
				dev_dbg(&adap->dev, "I2C SMBus Byte Read success: 0x%02x\n", ret);
		}
	}
	break;
	case 3:
	{ // Test for I2C_FUNC_SMBUS_BYTE_DATA
		{
			int ret = i2c_smbus_write_byte_data(client, 0x7E, 0x7E);
			if (ret < 0)
				dev_err(&adap->dev, "I2C SMBus Byte data Write failed: %d\n", ret);
			else
				dev_dbg(&adap->dev, "I2C SMBus Byte data Write success\n");
		}
		{
			int ret = i2c_smbus_read_byte_data(client, 0x7E);
			if (ret < 0)
				dev_err(&adap->dev, "I2C SMBus Byte data Read failed: %d\n", ret);
			else
				dev_dbg(&adap->dev, "I2C SMBus Byte data Read success: 0x%02x\n", ret);
		}
	}

	break;
	case 4:
	{ // Test for I2C_FUNC_SMBUS_WORD_DATA
		{
			int ret = i2c_smbus_write_word_data(client, 0x7E, 0x7E7E);
			if (ret < 0)
				dev_err(&adap->dev, "I2C SMBus word Write failed: %d\n", ret);
			else
				dev_dbg(&adap->dev, "I2C SMBus word Write success\n");
		}
		{
			int ret = i2c_smbus_read_word_data(client, 0x7E);
			if (ret < 0)
				dev_err(&adap->dev, "I2C SMBus word data Read failed: %d\n", ret);
			else
				dev_dbg(&adap->dev, "I2C SMBus word data Read success: 0x%04x\n", ret);
		}
	}
	break;
		/*
	case 5:
	{ // Test for I2C_FUNC_SMBUS_I2C_BLOCK Write
		u8 values[I2C_SMBUS_BLOCK_MAX];
		u8 len = 4;
		for (size_t i = 0; i < len; i++)
		{
			values[i] = i;
		}
		dev_dbg(&adap->dev, "Callingi2c_smbus_write_i2c_block_data\n");

		int ret = i2c_smbus_write_i2c_block_data(client, 0x7E, len, values);
		if (ret < 0)
			dev_err(&adap->dev, "I2C SMBus block Write failed: %d\n", ret);
		else
			dev_dbg(&adap->dev, "I2C SMBus block Write success\n");
	}
	break;
	case 6:
	{ // Test for I2C_FUNC_SMBUS_I2C_BLOCK Write
		u8 values[I2C_SMBUS_BLOCK_MAX];
		u8 len = I2C_SMBUS_BLOCK_MAX;
		for (size_t i = 0; i < len; i++)
		{
			values[i] = i;
		}

		int ret = i2c_smbus_read_i2c_block_data(client, 0x7E, I2C_SMBUS_BLOCK_MAX, values);
		if (ret < 0)
			dev_err(&adap->dev, "I2C SMBus block Read failed: %d\n", ret);
		else if (ret != I2C_SMBUS_BLOCK_MAX)
			dev_err(&adap->dev, "I2C SMBus block Read length doesn't match %d != %d\n", ret, len);
		else
		{
			dev_dbg(&adap->dev, "I2C SMBus block Read success\n");
			for (size_t i = 0; i < len; i++)
			{
				printk("0x%02X ", values[i]);
			}
			printk("\n");
		}
	}
		break;


	case 7:
	{ // Test for I2C_FUNC_SMBUS_QUICK
	  int ret = i2c_probe_func_quick_read(adap, testAddr);
	  if (ret < 0)
		dev_err(&adap->dev, "I2C SMBus Quick failed: %d\n", ret);
	  else
		dev_dbg(&adap->dev, "I2C SMBus Quick success\n");
	}
	break
	*/

	default:
	{
		pr_err("Invalid test case number: %d\n", test);
		// see the below functions for future test code if required
		/*
		s32 i2c_smbus_read_block_data(const struct i2c_client *client,
		u8 command, u8 *values);
		s32 i2c_smbus_write_block_data(const struct i2c_client *client,
		u8 command, u8 length, const u8 *values);

		u8 i2c_smbus_pec(u8 crc, u8 * p, size_t count);
		*/
	}
	break;
	}
	pr_debug("my_usb_devdrv - Tests Complete!\n\n\n\n");
	test++;

	if (client)
	{
		i2c_unregister_device(client);
		dev_dbg(&adap->dev, "Unregistered i2c client\n");
	}
}

static int my_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int ret;
	dev_info(&intf->dev, "Probe Function\n");

	// allocate memory for the device
	struct usb_i2c_dev *dev;
	dev = devm_kzalloc(&intf->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	// extract the usbdev from the interface
	dev->usb_dev = interface_to_usbdev(intf);
	if (dev->usb_dev == NULL)
	{
		dev_err(&intf->dev, "Error getting device from interface\n");
		return -1;
	}

	// populate the i2c adapter details

	// name that will appear in /sys/class/i2c-adapter
	strncpy(dev->adapter.name, ADAPTER_NAME, sizeof(dev->adapter.name) - 1);
	dev->adapter.name[sizeof(dev->adapter.name) - 1] = '\0'; // ensure null termination

	dev->adapter.owner = THIS_MODULE;
	dev->adapter.algo = &my_i2c_algorithm;
	// allows device to support hardware monitorring chips.
	dev->adapter.class = I2C_CLASS_HWMON;
	// This creates a proper device hierarchy in sysfs and helps with resource tracking.
	dev->adapter.dev.parent = &intf->dev;
	// This is useful in the algorithm functions to access our USB device.
	dev->adapter.algo_data = dev;
	// Let the kernel automatically assign a bus number to this adapter.
	// If this were a fixed adapter (like on a motherboard), you might set a specific number.
	dev->adapter.nr = -1;
	// Map this adapter to the device tree
	dev->adapter.dev.of_node = of_find_node_by_path("/i2c@USB4715");

	// save the struct to the given interface
	usb_set_intfdata(intf, dev);

	// Add the adapter to the user space
	ret = i2c_add_adapter(&dev->adapter);
	if (ret < 0)
	{
		of_node_put(dev->adapter.dev.of_node);
		dev_err(&intf->dev, "i2c_add_adapter failed with %d\n", ret);
		return -1;
	}

	// enable I2C bridge mode of hub.
	if (usb_control_msg(dev->usb_dev, usb_sndctrlpipe(dev->usb_dev, 0), I2C_ENTER_PASSTHRG, 0x41, 0x3131, 0, 0, 0, 20) < 0)
	{
		// delete the adapter because we failed to add it
		i2c_del_adapter(&dev->adapter);
		of_node_put(dev->adapter.dev.of_node);
		dev_err(&intf->dev, "Error Setting device into I2C Mode\n");
		return -1;
	}

	// RunTests(&dev->adapter);

	return 0;
}

static void my_usb_disconnect(struct usb_interface *intf)
{
	// proc_remove(proc_file);
	dev_info(&intf->dev, "Disconnect Function\n");

	struct usb_i2c_dev *dev = usb_get_intfdata(intf);
	of_node_put(dev->adapter.dev.of_node);
	i2c_del_adapter(&dev->adapter);
	usb_set_intfdata(intf, NULL);
}

static struct usb_driver my_usb_driver = {
	.name = "my_USB4715_drv",
	.id_table = usb_dev_table,
	.probe = my_usb_probe,
	.disconnect = my_usb_disconnect,
};

/**
 * @brief This function is called, when the module is loaded into the kernel
 */
static int __init my_init(void)
{
	int result;
	pr_debug("my_usb_devdrv - Init Function\n");
	result = usb_register(&my_usb_driver);
	if (result)
	{
		pr_err("my_usb_devdrv - Error during register!\n");
		return -result;
	}

	return 0;
}

/**
 * @brief This function is called, when the module is removed from the kernel
 */
static void __exit my_exit(void)
{
	pr_debug("my_usb_devdrv - Exit Function\n");
	usb_deregister(&my_usb_driver);
}

module_init(my_init);
module_exit(my_exit);

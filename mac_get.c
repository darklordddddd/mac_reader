#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#define DEVICE_NAME "mac_reader"
#define NET_CARD_VENDOR_ID 0x1022
#define NET_CARD_DEVICE_ID 0x2000

#define IO_BAR 0
#define RAP_REG 0x12
#define RDP_REG 0x10
#define MAC_ADDR 12
#define MAC_ADDR_LEN 3

static dev_t mac_dev;
static struct cdev mac_cdev;
short *mac_arr;
static int eof_flag;

// (VendorID, DeviceID)
static struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(NET_CARD_VENDOR_ID, NET_CARD_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static char num_to_char(char d)
{
	if (d >= 0 && d <= 9)
		return d + '0';

	if (d >= 10 && d <= 15)
		return (d - 10) + 'a';

	return -1;
}

static int pci_open(struct inode *inode, struct file *f)
{
	eof_flag = 0;
	return 0;
}

static int pci_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t pci_read(struct file *file,
				char *bf,
				size_t length,
				loff_t *offset)
{
	int i, j;

	char *temp = kmalloc(length, GFP_KERNEL);

	if (temp == NULL)
		return -1;

	if (eof_flag) {
		eof_flag = 0;
		kfree(temp);
		return 0;
	}

	for (i = 0, j = 0; i < (MAC_ADDR_LEN << 1); i++, j += 3) {
		temp[j] = num_to_char((mac_arr[i] >> 4) & 0xF);
		temp[j + 1] = num_to_char(mac_arr[i] & 0xF);
		temp[j + 2] = ':';
	}
	temp[17] = '\n';

	copy_to_user(bf, temp, 18);
	eof_flag = 1;
	kfree(temp);
	return 18;
}

static const struct file_operations pci_ops = {
	.owner		= THIS_MODULE,
	.read		= pci_read,
	.open		= pci_open,
	.release	= pci_release
};

static int pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret;
	resource_size_t start;
	unsigned short value;
	int i;

	pr_info("MAC_reader: in probe()\n");

	cdev_init(&mac_cdev, &pci_ops);
	mac_cdev.owner = THIS_MODULE;
	ret = cdev_add(&mac_cdev, mac_dev, 1);
	if (ret < 0) {
		dev_err(&(dev->dev), "Failed to init cdev struct\n");
		cdev_del(&mac_cdev);
		return -EBUSY;
	}

	if (pci_enable_device(dev)) {
		dev_err(&(dev->dev), "Can't enable PCI device\n");
		pci_disable_device(dev);
		return -EBUSY;
	}

	if (pci_request_regions(dev, DEVICE_NAME) < 0) {
		dev_err(&(dev->dev), "Can't request BARs\n");
		cdev_del(&mac_cdev);
		pci_release_regions(dev);
		pci_disable_device(dev);
		return -EBUSY;
	}

	if ((pci_resource_flags(dev, IO_BAR) & IORESOURCE_IO)
		!= IORESOURCE_IO) {
		dev_err(&(dev->dev), "IO_BAR isn't an I/O region\n");
		cdev_del(&mac_cdev);
		return -1;
	}

	mac_arr = kmalloc_array(6, sizeof(char), GFP_KERNEL);
	if (mac_arr == NULL)
		return -1;

	//пробуем читать MAC
	start = pci_resource_start(dev, IO_BAR);

	for (i = 0; i < MAC_ADDR_LEN; i++) {
		outw(MAC_ADDR + i, start + RAP_REG);
		value = inw(start + RDP_REG);
		mac_arr[i << 1] = value & 0xFF;
		mac_arr[(i << 1) + 1] = (value >> 8) & 0xFF;
		//pr_info("%02x - %02x\n", (value >> 8) & 0xFF, value & 0xFF);
	}

	return 0;
}

static void pci_remove(struct pci_dev *dev)
{
	pci_release_regions(dev);
}

static const struct pci_driver pci_driver = {
	.name		= DEVICE_NAME,
	.id_table	= pci_ids,
	.probe		= pci_probe,
	.remove	= pci_remove
};

static int __init mac_device_init(void)
{
	int ret;

	pr_info("MAC_reader: init\n");

	ret = alloc_chrdev_region(&mac_dev, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_alert("MAC_reader: Failed to get a major number\n");
		return -1;
	}
	pr_info("MAC_reader: major %d and minor %d\n",
	MAJOR(mac_dev), MINOR(mac_dev));

	ret = pci_register_driver(&pci_driver);
	if (ret < 0) {
		unregister_chrdev_region(mac_dev, 1);
		pr_alert("MAC_reader: pci_register_driver error");
		return -1;
	}

	return 0;
}

static void __exit mac_device_exit(void)
{
	kfree(mac_arr);
	pci_unregister_driver(&pci_driver);
	cdev_del(&mac_cdev);
	unregister_chrdev_region(mac_dev, 1);
	pr_info("MAC_reader: uninit completed\n");
}

module_init(mac_device_init);
module_exit(mac_device_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCI MAC address reader");
MODULE_AUTHOR("Sergey Samokhvalov/Ilya Vedmanov");
\ No newline at end of file

#include "myled.h"

unsigned int led_pin;

static ssize_t show_myled(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 1;
}

static ssize_t store_myled(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int input;
	input = simple_strtoul(buf, NULL, 0);
	gpio_set_value(led_pin, input);
	return count;
}

static DEVICE_ATTR(led_on_off, S_IWUSR | S_IRUGO, show_myled, store_myled);
static struct attribute *dev_attributes[] = {
	&dev_attr_led_on_off.attr,
	NULL
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attributes
};

static int __exit myled_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_group);
	return 0;
}

static int __init myled_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node;
	node = pdev->dev.of_node;
	if (!node)
		return -1;

	/* the name is property in device tree */
	led_pin  = of_get_named_gpio(node, "myled-gpios", 0);
	if (gpio_is_valid(led_pin))
		printk("pin valid\n");
	else
		printk("pin not valid\n");

	printk("led_pin = %d\n", led_pin);

	ret = sysfs_create_group(&pdev->dev.kobj, &dev_attr_group);
	if (ret)
		printk("create sysfs group error\n");

	/* the led_pin_name display in the sys/kernel/debug/gpio */
	ret = devm_gpio_request_one(&pdev->dev, led_pin, GPIOF_OUT_INIT_LOW, "myled_pin_name");
    if (ret < 0)
    {
        printk("Failed to request GPIO:%d, ERRNO:%d", (int)led_pin, ret);
        ret = -ENODEV;
    }

	return 0;
}

static const struct of_device_id myled_dt_ids[] = {
	{ .compatible = "myled", .data = NULL, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, myled_dt_ids);

static struct platform_driver myled_driver = {
	.driver		= {
		.name	= "myled",
		.owner = THIS_MODULE,
		.of_match_table = myled_dt_ids,
	},
	.probe		= myled_probe,
	.remove		= myled_remove,
};

module_platform_driver(myled_driver);

MODULE_DESCRIPTION("led driver");
MODULE_LICENSE("GPL");

#ifndef KERNEL_MODULE_TUTORIAL_EXAMPLEMOD_H
#define KERNEL_MODULE_TUTORIAL_EXAMPLEMOD_H

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vladimir Lagunov <lagunov.vladimir@gmail.com>");

static unsigned int gpio_pin = 17;

#endif //KERNEL_MODULE_TUTORIAL_EXAMPLEMOD_H

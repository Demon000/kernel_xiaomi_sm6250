/* drivers/input/keyreset.c
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/input.h>
#include <linux/keyreset.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/keycombo.h>
#include <linux/of.h>

struct keyreset_state {
	int restart_requested;
	struct platform_device *pdev_child;
	struct work_struct restart_work;
};

static void do_restart(struct work_struct *unused)
{
	machine_restart("recovery");
}

static void do_reset_fn(void *priv)
{
	struct keyreset_state *state = priv;
	if (state->restart_requested)
		panic("keyboard reset failed, %d", state->restart_requested);

	schedule_work(&state->restart_work);
	state->restart_requested = 1;
}

static int keyreset_probe(struct platform_device *pdev)
{
	struct keycombo_platform_data *pdata_child;
	struct device_node *np = pdev->dev.of_node;
	struct keyreset_state *state;
	struct property *prop;
	int key_count = 0, size, key, i;
	int *keys, *val;
	int ret = -ENOMEM;

	state = devm_kzalloc(&pdev->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->pdev_child = platform_device_alloc(KEYCOMBO_NAME,
							PLATFORM_DEVID_AUTO);
	if (!state->pdev_child)
		return -ENOMEM;

	state->pdev_child->dev.parent = &pdev->dev;
	INIT_WORK(&state->restart_work, do_restart);

	prop = of_find_property(np, "keys-down", NULL);
	if (!prop)
		return -ENOMEM;

	key_count = prop->length / sizeof(u32);
	keys = devm_kzalloc(&pdev->dev, key_count * sizeof(int), GFP_KERNEL);
	if (!keys)
		return -ENOMEM;

	val = prop->value;
	for (i = 0; i < key_count; i++) {
		key = (unsigned short) be32_to_cpup(val++);
		if (key == KEY_RESERVED || key >= KEY_MAX)
			continue;

		keys[i] = key;
	}

	size = sizeof(struct keycombo_platform_data) + sizeof(int) * (key_count + 1);
	pdata_child = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!pdata_child)
		goto error;

	memcpy(pdata_child->keys_down, keys,
			sizeof(int) * key_count);

	pdata_child->key_down_fn = do_reset_fn;
	pdata_child->priv = state;
	pdata_child->key_down_delay = 0;
	ret = platform_device_add_data(state->pdev_child, pdata_child, size);
	if (ret)
		goto error;

	platform_set_drvdata(pdev, state);
	return platform_device_add(state->pdev_child);
error:
	platform_device_put(state->pdev_child);
	return ret;
}

int keyreset_remove(struct platform_device *pdev)
{
	struct keyreset_state *state = platform_get_drvdata(pdev);
	platform_device_put(state->pdev_child);
	return 0;
}

static struct of_device_id keyreset_dt_match[] = {
	{ .compatible = "android,keyreset", },
	{ },
};
MODULE_DEVICE_TABLE(of, keyreset_dt_match);

struct platform_driver keyreset_driver = {
	.probe = keyreset_probe,
	.remove = keyreset_remove,
	.driver = {
		.name	= KEYRESET_NAME,
		.of_match_table = of_match_ptr(keyreset_dt_match),
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(keyreset_driver);

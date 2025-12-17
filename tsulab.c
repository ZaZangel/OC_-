#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/timekeeping.h>

#define PROC_FILENAME "tsulab"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Angelina");

static struct proc_dir_entry *tsu_proc_entry;

static ssize_t tsu_proc_read(struct file *file, char __user *user_buf, size_t count, loff_t *pos)
{
    if (*pos > 0)
        return 0;

    s64 current_s = ktime_get_real_seconds();
    s64 seconds_in_day = 24 * 60 * 60;
    s64 seconds_today = current_s % seconds_in_day;
    s64 seconds_until_midnight = seconds_in_day - seconds_today;
    s64 minutes_until_midnight = seconds_until_midnight / 60;

    if (minutes_until_midnight < 0)
        minutes_until_midnight = 0;

    char output_buffer[128];
    int length;

    length = snprintf(output_buffer, sizeof(output_buffer),
        "Minutes left for Cinderella to return home:\n%lld minutes\n",
        (long long)minutes_until_midnight);

    if (length < 0 || length >= sizeof(output_buffer))
        return -EINVAL;

    if (copy_to_user(user_buf, output_buffer, length))
        return -EFAULT;

    *pos = length;
    return length;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
static const struct proc_ops file_operations_tsu =
{
    .proc_read = tsu_proc_read,
};
#else
static const struct file_operations file_operations_tsu =
{
    .read = tsu_proc_read,
};
#endif

static int __init tsu_module_init(void)
{
    pr_info("Welcome to the Tomsk State University\n");

    tsu_proc_entry = proc_create(PROC_FILENAME, 0444, NULL, &file_operations_tsu);
    if (!tsu_proc_entry)
    {
        pr_err("tsulab: Failed to initialize /proc/%s\n", PROC_FILENAME);
        return -ENOMEM;
    }

    return 0;
}

static void __exit tsu_module_exit(void)
{
    proc_remove(tsu_proc_entry);
    pr_info("Tomsk State University forever!\n");
}

module_init(tsu_module_init);
module_exit(tsu_module_exit);

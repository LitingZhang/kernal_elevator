#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/syscalls.h>

int (*STUB_start_elevator)(void) = NULL;
EXPORT_SYMBOL(STUB_start_elevator);
SYSCALL_DEFINE0 (start_elevator) {
	if (STUB_start_elevator)
		return STUB_start_elevator();
	else
		return -ENOSYS;
}

int (*STUB_issue_request)(int,int,int) = NULL;
EXPORT_SYMBOL(STUB_issue_request);
SYSCALL_DEFINE3 (issue_request, int, start_floor, int, destination_floor, int, type) {
	if (STUB_issue_request)
		return STUB_issue_request(start_floor, destination_floor, type);
	else
		return -ENOSYS;
}




int (*STUB_stop_elevator)(void) = NULL;
EXPORT_SYMBOL(STUB_stop_elevator);
SYSCALL_DEFINE0 (stop_elevator) {
	if (STUB_stop_elevator)
		return STUB_stop_elevator();
	else
		return -ENOSYS;
}

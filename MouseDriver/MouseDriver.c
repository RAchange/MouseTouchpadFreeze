#include <ntddk.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

#ifndef SetFlag
#define SetFlag(_F,_SF) ((_F) |= (_SF))
#endif
#ifndef ClearFlag
#define ClearFlag(_F,_SF) ((_F) &= ~(_SF))
#endif
#define CCP_MAX_COM_ID 32

// 過濾設備和真實設備
static DEVICE_OBJECT s_fltobj = { 0 };
static DEVICE_OBJECT s_nextobj = { 0 };

// 打開一個端口設備
PDEVICE_OBJECT ccpOpenMouse(NTSTATUS* status)
{
	UNICODE_STRING name_str;
	static WCHAR name[32] = { 0 };
	PFILE_OBJECT fileobj = NULL;
	PDEVICE_OBJECT devobj = NULL;

	// 輸入裝置 symbolic link
	memset(name, 0, sizeof(WCHAR) * 32);
	RtlStringCchPrintfW(
		name, 32,
		L"\\Device\\ACPI\\SYN1509\\4&1B768927&0");
	RtlInitUnicodeString(&name_str, name);

	// 打開對應設備
	*status = IoGetDeviceObjectPointer(&name_str, FILE_ALL_ACCESS, &fileobj, &devobj);
	if (*status == STATUS_SUCCESS)
		ObDereferenceObject(fileobj);

	return devobj;
}

NTSTATUS
ccpAttachDevice(
	PDRIVER_OBJECT driver,
	PDEVICE_OBJECT oldobj,
	PDEVICE_OBJECT* fltobj,
	PDEVICE_OBJECT* next)
{
	NTSTATUS status;
	PDEVICE_OBJECT topdev = NULL;

	// 生成設備，以綁定滑鼠裝置
	status = IoCreateDevice(driver,
		0,
		NULL,
		oldobj->DeviceType,
		0,
		FALSE,
		fltobj);

	if (status != STATUS_SUCCESS)
		return status;

	// 拷貝一些重要的參數
	if (oldobj->Flags & DO_BUFFERED_IO)
		(*fltobj)->Flags |= DO_BUFFERED_IO;
	if (oldobj->Flags & DO_DIRECT_IO)
		(*fltobj)->Flags |= DO_DIRECT_IO;
	if (oldobj->Flags & DO_BUFFERED_IO)
		(*fltobj)->Flags |= DO_BUFFERED_IO;
	if (oldobj->Characteristics & FILE_DEVICE_SECURE_OPEN)
		(*fltobj)->Characteristics |= FILE_DEVICE_SECURE_OPEN;
	(*fltobj)->Flags |= DO_POWER_PAGABLE;
	// 綁定剛剛創建的設備到滑鼠裝置
	topdev = IoAttachDeviceToDeviceStack(*fltobj, oldobj);
	if (topdev == NULL)
	{
		// 如果綁定失敗了，銷毀設備，重新來過。
		IoDeleteDevice(*fltobj);
		*fltobj = NULL;
		status = STATUS_UNSUCCESSFUL;
		return status;
	}
	*next = topdev;

	// 綁定滑鼠的裝置啟動。
	(*fltobj)->Flags = (*fltobj)->Flags & ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}


#define DELAY_ONE_MICROSECOND (-10)
#define DELAY_ONE_MILLISECOND (DELAY_ONE_MICROSECOND*1000)
#define DELAY_ONE_SECOND (DELAY_ONE_MILLISECOND*1000)

void ccpUnload(PDRIVER_OBJECT drv)
{
	ULONG i;
	LARGE_INTEGER interval;

	// 首先解除綁定
	if (&s_nextobj != NULL)
		IoDetachDevice(&s_nextobj);

	// 睡眠5秒。等待所有irp處理結束
	interval.QuadPart = (5 * 1000 * DELAY_ONE_MILLISECOND);
	KeDelayExecutionThread(KernelMode, FALSE, &interval);

	// 刪除這些設備
	if (&s_fltobj != NULL)
		IoDeleteDevice(&s_fltobj);
}

NTSTATUS ccpDispatch(PDEVICE_OBJECT device, PIRP irp)
{
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS status;
	ULONG i, j;


	// 首先得知道發送給了哪個設備。
	// 是前面的代碼保存好的，都在s_fltobj中。
	if (&s_fltobj == device)
	{
		// 所有電源操作，全部直接放過。
		if (irpsp->MajorFunction == IRP_MJ_POWER)
		{
			// 直接發送，然後返回說已經被處理了。
			PoStartNextPowerIrp(irp);
			IoSkipCurrentIrpStackLocation(irp);
			return PoCallDriver(&s_nextobj, irp);
		}
		// 此外我們只過濾寫請求。寫請求的話，獲得緩衝區以及其長度。
		if (irpsp->MajorFunction == IRP_MJ_WRITE)
		{
			// 如果是寫入，先獲得長度
			ULONG len = irpsp->Parameters.Write.Length;
			// 然後獲得緩衝區
			PUCHAR buf = NULL;
			if (irp->MdlAddress != NULL)
				buf =
				(PUCHAR)
				MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
			else
				buf = (PUCHAR)irp->UserBuffer;
			if (buf == NULL)
				buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;

			// 將請求資料清空
			memset(buf, 0, len);
		}

		// 這些請求直接下發執行。
		IoSkipCurrentIrpStackLocation(irp);
		return IoCallDriver(&s_nextobj, irp);
	}

	// 如果根本就不在被綁定的設備中，那是有問題的，直接返回參數錯誤。
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path)
{
	size_t i;
	PDEVICE_OBJECT com_ob = NULL;
	NTSTATUS status;

	// 所有的分發函數都設置成一樣的。
	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		driver->MajorFunction[i] = ccpDispatch;
	}

	// 支持動態卸載。
	driver->DriverUnload = ccpUnload;


	com_ob = ccpOpenMouse(i, &status);

	// 在這裡綁定。並不管綁定是否成功。
	ccpAttachDevice(driver, com_ob, &s_fltobj, &s_nextobj);

	// 直接返回成功即可。
	return STATUS_SUCCESS;
}
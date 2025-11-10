#include <Protocol/ScsiIo.h>
#include <Protocol/ScsiPassThruExt.h>
#include "Common.h"

// Eject the CD
EFI_STATUS Eject(IN EFI_HANDLE CdHandle) {
	EFI_STATUS Status;
	EFI_DEVICE_PATH_PROTOCOL *Dp;
	EFI_HANDLE ScsiHandle;
	EFI_EXT_SCSI_PASS_THRU_PROTOCOL *ScsiPt;
	UINT8 *Target;
	UINT64 Lun;
	EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET Pkt;
	UINT8 SenseData[32];

	// Get device path
	Status = gBS->HandleProtocol(
		CdHandle,
		&gEfiDevicePathProtocolGuid,
		(VOID**)&Dp
	);
	if (EFI_ERROR(Status)) return Status;

	// Get SCSI handle
	Status = gBS->LocateDevicePath(
		&gEfiExtScsiPassThruProtocolGuid,
		&Dp,
		&ScsiHandle
	);
	if (EFI_ERROR(Status)) return Status;

	Status = gBS->HandleProtocol(
		ScsiHandle,
		&gEfiExtScsiPassThruProtocolGuid,
		(VOID**)&ScsiPt
	);
	if (EFI_ERROR(Status)) return Status;

	// Translate device path node
	Status = ScsiPt->GetTargetLun(ScsiPt, Dp, &Target, &Lun);
	if (EFI_ERROR(Status)) return Status;

	// Build packet
	ZeroMem(&Pkt, sizeof(Pkt));
	Pkt.SenseData = SenseData;
	Pkt.SenseDataLength = sizeof(SenseData);
	Pkt.Timeout = EFI_TIMER_PERIOD_SECONDS(3);

	// Send ALLOW_MEDIUM_REMOVAL
	UINT8 Cdb[6] = { 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00 };
	Pkt.Cdb = Cdb;
	Pkt.CdbLength = sizeof(Cdb);
	Status = ScsiPt->PassThru(ScsiPt, Target, Lun, &Pkt, NULL);
	if (EFI_ERROR(Status)) return Status;

	// Send START_STOP with LoEj
	Cdb[0] = 0x1B;
	Cdb[4] = 0x02;
	Status = ScsiPt->PassThru(ScsiPt, Target, Lun, &Pkt, NULL);
	return Status;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
	EFI_FILE_PROTOCOL *Root;
	EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SrcFs;
	EFI_STATUS Status;
	EFI_HANDLE *Handles;
	UINTN HandleCount = 0;

	PrintStatus(L"Initializing...");

	// Retrieve information about the loaded image
	Status = gBS->HandleProtocol(
		ImageHandle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&LoadedImage
	);
	if (EFI_ERROR(Status)) return Status;

	// Open the FS of the image’s drive
	Status = gBS->HandleProtocol(
		LoadedImage->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&SrcFs
	);
	if (EFI_ERROR(Status) || EFI_ERROR(SrcFs->OpenVolume(SrcFs, &Root))) return Status;

	Status = DisplayLogo(Root, L"\\EFI\\Logo.bmp");
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Could not load logo: %r", Status);
	}

	CloseVolume(Root);

	PrintStatus(L"Ejecting installation drive...");
	Status = gBS->LocateHandleBuffer(
		ByProtocol,
		&gEfiScsiIoProtocolGuid,
		NULL,
		&HandleCount,
		&Handles
	);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Unable to enumerate CD-ROMs: %r", Status);
		return ChainLoad(LoadedImage->DeviceHandle, L"\\EFI\\OC\\OpenCore.efi");
	}

	// Here, we are quite aggressive as we are ejecting all removable SCSI drives.
	for (UINTN i = 0; i < HandleCount; i++) {
		Eject(Handles[i]);
	}
	FreePool(Handles);

	return ChainLoad(LoadedImage->DeviceHandle, L"\\EFI\\OC\\OpenCore.efi");
}

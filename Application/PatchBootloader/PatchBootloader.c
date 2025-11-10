#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>
#include "Common.h"
#include "Copy.h"

#define MAX_DISKS 16

STATIC CONST EFI_GUID gAppleApfsPartitionTypeGuid = { 0x7C3457EF, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } };

typedef struct {
	UINT32 MediaId;
	EFI_HANDLE EfiPartHandle;
	BOOLEAN HasEfi;
	BOOLEAN HasApfs;
} DISK_ENTRY;

// Find the first disk that contains an EFI partition followed by an APFS partition
EFI_STATUS FindMacOSEFI(OUT EFI_HANDLE *EfiPartHandle) {
	DISK_ENTRY Disks[MAX_DISKS];
	EFI_HANDLE *Handles;
	EFI_STATUS Status;
	UINTN PartCount = 0, DiskCount = 0;

	// Enumerate block devices
	Status = gBS->LocateHandleBuffer(
		ByProtocol,
		&gEfiBlockIoProtocolGuid,
		NULL,
		&PartCount,
		&Handles
	);
	if (EFI_ERROR(Status)) return Status;

	// Iterate over them
	for (UINTN i = 0; i < PartCount; i++) {
		EFI_PARTITION_INFO_PROTOCOL *PartInfo;
		EFI_BLOCK_IO_PROTOCOL *BlkIo;

		// Get the partition information
		Status = gBS->HandleProtocol(
			Handles[i],
			&gEfiPartitionInfoProtocolGuid,
			(VOID**)&PartInfo
		);
		// Ignore non-GPT records
		if (EFI_ERROR(Status) || PartInfo->Type != PARTITION_TYPE_GPT) continue;

		// Identify parent disk
		Status = gBS->HandleProtocol(
			Handles[i],
			&gEfiBlockIoProtocolGuid,
			(VOID**)&BlkIo
		);
		if (EFI_ERROR(Status)) return Status;

		// Search the corresponding disk entry
		DISK_ENTRY *Disk = NULL;
		UINT32 ParentId = BlkIo->Media->MediaId;
		for (UINTN j = 0; j < DiskCount; j++) {
			if (Disks[j].MediaId == ParentId) {
				Disk = &Disks[j];
				break;
			}
		}

		// Create a disk entry if necessary
		if (Disk == NULL) {
			if (DiskCount >= MAX_DISKS) {
				continue;
			}
			Disk = &Disks[DiskCount++];
			Disk->MediaId = ParentId;
			Disk->HasEfi = FALSE;
			Disk->HasApfs = FALSE;
			Disk->EfiPartHandle = NULL;
		}

		// Check partition type
		if (CompareGuid(&PartInfo->Info.Gpt.PartitionTypeGUID, &gEfiPartTypeSystemPartGuid)) {
			Disk->HasEfi = TRUE;
			Disk->EfiPartHandle = Handles[i];
		} else if (CompareGuid(&PartInfo->Info.Gpt.PartitionTypeGUID, &gAppleApfsPartitionTypeGuid)) {
			Disk->HasApfs = TRUE;
		}

		// If macOS’ disk has been found, return the EFI partition handle
		if (Disk->HasEfi && Disk->HasApfs) {
			*EfiPartHandle = Disk->EfiPartHandle;
			FreePool(Handles);
			return EFI_SUCCESS;
		}
	}

	FreePool(Handles);
	return EFI_NOT_FOUND;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
	EFI_FILE_PROTOCOL *Root;
	EFI_HANDLE EfiPart;
	EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SrcFs;
	EFI_STATUS Status;

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

	PrintStatus(L"Looking for MacOS...");
	Status = FindMacOSEFI(&EfiPart);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"No suitable disk found: %r", Status);
		CloseVolume(Root);
		return ChainLoad(LoadedImage->DeviceHandle, L"\\EFI\\OC\\OpenCore.efi");
	}

	PrintStatus(L"Copying Stage-2 bootloader...");
	Status = CopyStage2(Root, EfiPart);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Failed to install Stage-2 bootloader: %r", Status);
		CloseVolume(Root);
		return ChainLoad(LoadedImage->DeviceHandle, L"\\EFI\\OC\\OpenCore.efi");
	}

	CloseVolume(Root);

	Status = ChainLoad(EfiPart, L"\\EFI\\BOOT\\BOOTX64.EFI");
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Unable to chainload: %r", Status);
		return ChainLoad(LoadedImage->DeviceHandle, L"\\EFI\\OC\\OpenCore.efi");
	}

	return EFI_SUCCESS;
}

#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PartitionInfo.h>
#include "Copy.h"
#include "Logo.h"
#include "Print.h"

#define MAX_DISKS 16

STATIC CONST EFI_GUID gAppleApfsPartitionTypeGuid = { 0x7C3457EF, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } };

typedef struct {
	UINT32 MediaId;
	EFI_HANDLE EfiPartHandle;
	BOOLEAN HasEfi;
	BOOLEAN HasApfs;
} DISK_ENTRY;

// Find the first disk that contains an EFI partition followed by an APFS partition
EFI_STATUS FindMacOSEFI (OUT EFI_HANDLE *EfiPartHandle) {
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

// Eject the CD
EFI_STATUS EjectCd(IN EFI_HANDLE CdHandle) {
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *BlkIo;

	Status = gBS->HandleProtocol(
		CdHandle,
		&gEfiBlockIoProtocolGuid,
		(VOID**)&BlkIo
	);
	if (!EFI_ERROR(Status) && BlkIo->Media->RemovableMedia) {
		BlkIo->Reset(BlkIo, TRUE);
	}

	return Status;
}

// Chainload \EFI\OC\OpenCore.efi in a given partition
EFI_STATUS ChainLoad(IN EFI_HANDLE PartHandle) {
	EFI_STATUS Status;
	EFI_DEVICE_PATH_PROTOCOL *Dp;
	EFI_HANDLE Image;

	PrintStatus(L"Handing over to OpenCore...\n");

	// Create Part:\EFI\OC\OpenCore.efi device path
	Dp = FileDevicePath(PartHandle, L"\\EFI\\OC\\OpenCore.efi");

	Status = gBS->LoadImage(
		FALSE,
		gImageHandle,
		Dp,
		NULL,
		0,
		&Image
	);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Chainload failed: %r", Status);
		return Status;
	}

	return gBS->StartImage(Image, NULL, NULL);
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
		return ChainLoad(LoadedImage->DeviceHandle);
	}

	PrintStatus(L"Copying Stage-2 bootloader...");
	Status = CopyStage2(Root, EfiPart);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Failed to install Stage-2 bootloader: %r", Status);
		return ChainLoad(LoadedImage->DeviceHandle);
	}

	PrintStatus(L"Ejecting installation drive...");
	Status = EjectCd(LoadedImage->DeviceHandle);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Unable to eject disk: %r", Status);
	}

	Status = ChainLoad(EfiPart);
	if (EFI_ERROR(Status)) {
		return ChainLoad(LoadedImage->DeviceHandle);
	}

	return EFI_SUCCESS;
}

#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Print.h"

// Chainload \EFI\OC\OpenCore.efi in a given partition
EFI_STATUS ChainLoad(IN EFI_HANDLE PartHandle, IN CHAR16 *Path) {
	EFI_STATUS Status;
	EFI_DEVICE_PATH_PROTOCOL *Dp;
	EFI_HANDLE Image;

	PrintStatus(L"Handing over to OpenCore...\n");

	// Create Part:\EFI\OC\OpenCore.efi device path
	Dp = FileDevicePath(PartHandle, Path);

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

// Close the given volume
VOID CloseVolume(IN EFI_FILE_PROTOCOL *Volume) {
	EFI_STATUS Status = Volume->Close(Volume);
	if (EFI_ERROR(Status)) {
		PrintStatusR(L"Unable to close root directory: %r", Status);
	}
}

#include <Library/BaseMemoryLib.h>
#include <Library/FileHandleLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#pragma pack(1)
typedef struct {
	UINT16 Magic;
	UINT32 Size;
	UINT32 Reserved;
	UINT32 Offset;
} BMP_FILE_HEADER;

typedef struct {
	UINT32 Size;
	UINT32 Width;
	UINT32 Height;
	UINT16 Planes;
	UINT16 BitsPerPixel;
	UINT32 Compression;
	UINT32 ImageSize;
	UINT32 XPixelsPerMeter;
	UINT32 YPixelsPerMeter;
	UINT32 Colors;
	UINT32 ImportantColors;
} BMP_IMAGE_HEADER;
#pragma pack()

// Load a bitmap file to a BLT buffer
EFI_STATUS LoadBmpToBlt(IN EFI_FILE_PROTOCOL *Root, IN CHAR16 *FileName, OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL **OutBlt, OUT UINTN *Width, OUT UINTN *Height) {
	BMP_FILE_HEADER BmpHdr;
	BMP_IMAGE_HEADER InfoHdr;
	EFI_FILE_INFO *FileInfo;
	EFI_FILE_PROTOCOL *File;
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer, *Dst;
	EFI_STATUS Status;
	UINT8 *FileBuffer, *Src;
	UINTN FileSize, InfoSize, Row, Col;

	// Open BMP file
	Status = Root->Open(
		Root,
		&File,
		FileName,
		EFI_FILE_MODE_READ,
		0
	);
	if (EFI_ERROR(Status)) {
		return Status;
	}

	// Get file size
	InfoSize = SIZE_OF_EFI_FILE_INFO + 256 * sizeof(CHAR16);
	FileInfo = AllocatePool(InfoSize);
	Status = File->GetInfo(
		File,
	   	&gEfiFileInfoGuid,
	   	&InfoSize,
	   	FileInfo
	);
	if (EFI_ERROR(Status)) {
		File->Close(File);
		return Status;
	}
	FileSize = FileInfo->FileSize;
	FreePool(FileInfo);

	// Read the entire file
	FileBuffer = AllocatePool(FileSize);
	Status = File->Read(File, &FileSize, FileBuffer);
	File->Close(File);
	if (EFI_ERROR(Status)) {
		FreePool(FileBuffer);
		return Status;
	}

	// Parse BMP header
	CopyMem(&BmpHdr, FileBuffer, sizeof(BMP_FILE_HEADER));
	if (BmpHdr.Magic != 0x4D42) {
		FreePool(FileBuffer);
		return EFI_UNSUPPORTED;
	}

	// Parse data header
	CopyMem(&InfoHdr, FileBuffer + sizeof(BMP_FILE_HEADER), sizeof(BMP_IMAGE_HEADER));
	*Width = InfoHdr.Width;
	*Height = InfoHdr.Height;
	if (InfoHdr.BitsPerPixel != 24 || InfoHdr.Compression != 0) {
		FreePool(FileBuffer);
		return EFI_UNSUPPORTED;
	}

	// Allocate BLT buffer
	BltBuffer = AllocatePool(*Width * (*Height) * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
	if (BltBuffer == NULL) {
		FreePool(FileBuffer);
		return EFI_OUT_OF_RESOURCES;
	}

	// Process the BMP rows and reverse their order in the output buffer
	Src = FileBuffer + BmpHdr.Offset;
	Dst = BltBuffer + (*Width) * (*Height - 1);
	for (Row = 0; Row < *Height; Row++) {
		for (Col = 0; Col < *Width; Col++) {
			EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel = { Src[0], Src[1], Src[2], 0xFF };
			*Dst++ = Pixel;
			Src += 3;
		}
		// Rows are padded to 4-byte. We use the fact that (4 - (3n mod 4)) mod 4 = n mod 4.
		Src += *Width % 4;
		Dst -= 2 * (*Width);
	}
	FreePool(FileBuffer);

	*OutBlt = BltBuffer;
	return EFI_SUCCESS;
}


// Display a centered logo
EFI_STATUS DisplayLogo(IN EFI_FILE_PROTOCOL *Root, IN CHAR16 *Path) {
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL *LogoBlt;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
	EFI_STATUS Status;
	UINTN LogoHeight;
	UINTN LogoWidth;

	Status = gBS->LocateProtocol(
		&gEfiGraphicsOutputProtocolGuid,
		NULL,
		(VOID**)&Gop
	);
	if (EFI_ERROR(Status)) return Status;

	// Load the BMP from file
	Status = LoadBmpToBlt(
		Root,
		Path,
	   	&LogoBlt,
	   	&LogoWidth,
	   	&LogoHeight
	);
	if (EFI_ERROR(Status)) return Status;

	// Display the image
	return Gop->Blt(
		Gop,
		LogoBlt,
		EfiBltBufferToVideo,
		0,
		0,
		((Gop->Mode->Info->HorizontalResolution - LogoWidth) / 2),
	   	((Gop->Mode->Info->VerticalResolution - LogoHeight) / 2),
		LogoWidth,
		LogoHeight,
		0
	);
}

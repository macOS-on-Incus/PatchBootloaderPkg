#include <Library/FileHandleLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define COPY_BUFFER_SIZE  4096

// Copy a single file
EFI_STATUS CopyFile(IN EFI_FILE_PROTOCOL *SrcDir, IN EFI_FILE_PROTOCOL *DstDir, IN CHAR16 *FileName) {
	EFI_STATUS Status;
	EFI_FILE_PROTOCOL *SrcFile = NULL, *DstFile = NULL;
	UINTN ReadSize;

	// Open source file
	Status = SrcDir->Open(
		SrcDir,
		&SrcFile,
		FileName,
		EFI_FILE_MODE_READ,
		0
	);
	if (EFI_ERROR(Status)) return Status;

	// Try to delete destination file
	Status = DstDir->Open(
		DstDir,
		&DstFile,
		FileName,
		EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
		0
	);
	if (!EFI_ERROR(Status)) {
		DstFile->Delete(DstFile);
	}

	// Create destination file
	Status = DstDir->Open(
		DstDir,
		&DstFile,
		FileName,
		EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
		0
	);
	if (EFI_ERROR(Status)) {
		SrcFile->Close(SrcFile);
		return Status;
	}

	// Allocate copy buffer
	VOID *Buffer = AllocatePool(COPY_BUFFER_SIZE);
	if (Buffer == NULL) {
		DstFile->Close(DstFile);
		SrcFile->Close(SrcFile);
		return Status;
	}

	// Read/Write loop
	while (TRUE) {
		ReadSize = COPY_BUFFER_SIZE;
		Status = SrcFile->Read(SrcFile, &ReadSize, Buffer);
		if (EFI_ERROR(Status) || ReadSize == 0) break;

		Status = DstFile->Write(DstFile, &ReadSize, Buffer);
		if (EFI_ERROR(Status)) break;
	}

	gBS->FreePool(Buffer);
	DstFile->Close(DstFile);
	SrcFile->Close(SrcFile);

	// Treat EOF as OK
	if (Status == EFI_END_OF_FILE) {
		return EFI_SUCCESS;
	}
	return Status;
}

// Copy files recursively
EFI_STATUS CopyDirectoryRecursive(IN EFI_FILE_PROTOCOL *SrcDir, IN EFI_FILE_PROTOCOL *DstDir) {
	EFI_STATUS Status;
	EFI_FILE_PROTOCOL *NewSrcDir = NULL, *NewDstDir = NULL;
	EFI_FILE_INFO *FileInfo = NULL;
	UINTN BufferSize;
	CHAR16 *Name;

	// Allocate a buffer large enough for EFI_FILE_INFO + filename
	BufferSize = SIZE_OF_EFI_FILE_INFO + 256 * sizeof(CHAR16);
	Status = gBS->AllocatePool(EfiBootServicesData, BufferSize, (VOID**)&FileInfo);
	if (EFI_ERROR(Status)) return Status;

	// Start reading entries at the beginning
	SrcDir->SetPosition(SrcDir, 0);

	// Iterate over the directory
	while (TRUE) {
		UINTN InfoSize = BufferSize;
		Status = SrcDir->Read(SrcDir, &InfoSize, FileInfo);
		if (EFI_ERROR(Status) || InfoSize == 0) break;

		Name = FileInfo->FileName;
		// Skip . and ..
		if (StrCmp(Name, L".") == 0 || StrCmp(Name, L"..") == 0) continue;

		// If the entry is a sub-directory, create a directory and recurse
		if (FileInfo->Attribute & EFI_FILE_DIRECTORY) {
			Status = DstDir->Open(
				DstDir,
				&NewDstDir,
				Name,
				EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
				EFI_FILE_DIRECTORY
			);
			if (EFI_ERROR(Status)) break;

			Status = SrcDir->Open(
				SrcDir,
				&NewSrcDir,
				Name,
				EFI_FILE_MODE_READ,
				0
			);
			if (EFI_ERROR(Status)) {
				NewDstDir->Close(NewDstDir);
				break;
			}

			// Recursive copy
			Status = CopyDirectoryRecursive(NewSrcDir, NewDstDir);
			NewDstDir->Close(NewDstDir);
			NewSrcDir->Close(NewSrcDir);
			if (EFI_ERROR(Status)) break;
		}
		// Else, simply copy the file
		else {
			Status = CopyFile(SrcDir, DstDir, Name);
			if (EFI_ERROR(Status)) break;
		}
	}

	gBS->FreePool(FileInfo);

	// Treat EOF as OK
	if (Status == EFI_END_OF_FILE)
		return EFI_SUCCESS;
	return Status;
}

// Copy the Stage-2 bootloader to the root EFI partition
EFI_STATUS CopyStage2(EFI_FILE_PROTOCOL *SrcRoot, IN EFI_HANDLE EfiPart) {
	EFI_STATUS Status;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *DstFs;
	EFI_FILE_PROTOCOL *DstRoot, *SrcDir;

	// Get a handle to Stage-2 directory
	Status = SrcRoot->Open(
		SrcRoot,
		&SrcDir,
		L"Stage-2",
		EFI_FILE_MODE_READ,
		0
	);
	if (EFI_ERROR(Status)) {
		SrcRoot->Close(SrcRoot);
		return Status;
	}

	// Open the FS of the target EFI partition
	Status = gBS->HandleProtocol(
		EfiPart,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&DstFs
	);
	if (EFI_ERROR(Status) || EFI_ERROR(DstFs->OpenVolume(DstFs, &DstRoot))) {
		SrcDir->Close(SrcDir);
		SrcRoot->Close(SrcRoot);
		return Status;
	}

	// Copy recursively Stage-2 directory contents
	Status = CopyDirectoryRecursive(
		SrcDir,
		DstRoot
	);

	SrcDir->Close(SrcDir);
	SrcRoot->Close(SrcRoot);
	DstRoot->Close(DstRoot);

	return Status;
}

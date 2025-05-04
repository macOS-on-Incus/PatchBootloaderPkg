#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

// Print a status line
EFI_STATUS PrintStatus(IN CHAR16 *Text) {
	UINTN Columns, Rows;
	EFI_STATUS Status;

	// Query console dimensions
	Status = gST->ConOut->QueryMode(
		gST->ConOut,
	   	gST->ConOut->Mode->Mode,
	   	&Columns,
	   	&Rows
	);
	if (EFI_ERROR(Status)) return Status;

	// Initialize a blank line
	CHAR16 *Blank = AllocatePool((Columns + 1) * sizeof (CHAR16));
	if (Blank == NULL) return EFI_OUT_OF_RESOURCES;
	SetMem16(Blank, Columns * sizeof (CHAR16), L' ');
	Blank[Columns] = L'\0';

	// Position the cursor
	Status = gST->ConOut->SetCursorPosition(
		gST->ConOut,
	   	0,
	   	Rows - 3
	);
	if (EFI_ERROR(Status)) return Status;

	// Print the blank line
	Status = gST->ConOut->OutputString(gST->ConOut, Blank);
	if (EFI_ERROR(Status)) return Status;

	FreePool (Blank);

	// Print the status line
	Status = gST->ConOut->SetCursorPosition(
		gST->ConOut,
	   	(Columns - StrLen(Text)) / 2,
	   	Rows - 3
	);
	if (EFI_ERROR(Status)) return Status;

	return gST->ConOut->OutputString(gST->ConOut, Text);
}

// Print a status line
EFI_STATUS PrintStatusR(IN CHAR16 *Text, IN EFI_STATUS R) {
	CHAR16 PrintBuf[128];
	UnicodeSPrint(PrintBuf, sizeof(PrintBuf), Text, R);
	return PrintStatus(PrintBuf);
}

// Print a status line
EFI_STATUS PrintStatusS(IN CHAR16 *Text, IN CHAR16 *S) {
	CHAR16 PrintBuf[128];
	UnicodeSPrint(PrintBuf, sizeof(PrintBuf), Text, S);
	return PrintStatus(PrintBuf);
}

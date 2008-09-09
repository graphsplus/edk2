/** @file
  HII Library implementation that uses DXE protocols and services.

  Copyright (c) 2006 - 2008, Intel Corporation<BR>
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "InternalHiiLib.h"

CONST EFI_HII_DATABASE_PROTOCOL   *mHiiDatabaseProt = NULL;
CONST EFI_HII_STRING_PROTOCOL     *mHiiStringProt = NULL;

/**

  This function locate Hii relative protocols for later usage.

**/
VOID
LocateHiiProtocols (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mHiiStringProt != NULL && mHiiDatabaseProt != NULL) {
    //
    // Only need to initialize the protocol instance once.
    //
    return;
  }

  Status = gBS->LocateProtocol (&gEfiHiiDatabaseProtocolGuid, NULL, (VOID **) &mHiiDatabaseProt);
  ASSERT_EFI_ERROR (Status);

  Status = gBS->LocateProtocol (&gEfiHiiStringProtocolGuid, NULL, (VOID **) &mHiiStringProt);
  ASSERT_EFI_ERROR (Status);
}



/**
  This funciton build the package list based on the package number,
  the GUID of the package list and the list of pointer which point to
  package header that defined by UEFI VFR compiler and StringGather
  tool.

  #pragma pack (push, 1)
  typedef struct {
    UINT32                  BinaryLength;
    EFI_HII_PACKAGE_HEADER  PackageHeader;
  } TIANO_AUTOGEN_PACKAGES_HEADER;
  #pragma pack (pop)

  If there is not enough resource for the new package list,
  the function will ASSERT.

  @param NumberOfPackages The number of packages be 
  @param GuidId          The GUID for the package list to be generated.
  @param Marker          The variable argument list. Each entry represent a specific package header that is
                         generated by VFR compiler and StrGather tool. The first 4 bytes is a UINT32 value
                         that indicate the overall length of the package.

  @return The pointer to the package list header.

**/
EFI_HII_PACKAGE_LIST_HEADER *
InternalHiiLibPreparePackages (
  IN UINTN           NumberOfPackages,
  IN CONST EFI_GUID  *GuidId,
  IN VA_LIST         Marker
  )
{
  EFI_HII_PACKAGE_LIST_HEADER *PackageListHeader;
  UINT8                       *PackageListData;
  UINT32                      PackageListLength;
  UINT32                      PackageLength;
  EFI_HII_PACKAGE_HEADER      PackageHeader;
  UINT8                       *PackageArray;
  UINTN                       Index;
  VA_LIST                     MarkerBackup;

  PackageListLength = sizeof (EFI_HII_PACKAGE_LIST_HEADER);

  MarkerBackup = Marker;

  //
  // Count the lenth of the final package list.
  //
  for (Index = 0; Index < NumberOfPackages; Index++) {
    CopyMem (&PackageLength, VA_ARG (Marker, VOID *), sizeof (UINT32));
    //
    // Do not count the BinaryLength field.
    //
    PackageListLength += (PackageLength - sizeof (UINT32));
  }

  //
  // Include the lenght of EFI_HII_PACKAGE_END
  //
  PackageListLength += sizeof (EFI_HII_PACKAGE_HEADER);
  PackageListHeader = AllocateZeroPool (PackageListLength);
  ASSERT (PackageListHeader != NULL);
  
  CopyGuid (&PackageListHeader->PackageListGuid, GuidId);
  PackageListHeader->PackageLength = PackageListLength;

  PackageListData = ((UINT8 *) PackageListHeader) + sizeof (EFI_HII_PACKAGE_LIST_HEADER);

  Marker = MarkerBackup;
  //
  // Prepare the final package list.
  //
  for (Index = 0; Index < NumberOfPackages; Index++) {
    PackageArray = (UINT8 *) VA_ARG (Marker, VOID *);
    //
    // CopyMem is used for UINT32 to cover the unaligned address access.
    //
    CopyMem (&PackageLength, PackageArray, sizeof (UINT32));
    PackageLength  -= sizeof (UINT32);
    PackageArray += sizeof (UINT32);
    CopyMem (PackageListData, PackageArray, PackageLength);
    PackageListData += PackageLength;
  }

  //
  // Append EFI_HII_PACKAGE_END
  //
  PackageHeader.Type = EFI_HII_PACKAGE_END;
  PackageHeader.Length = sizeof (EFI_HII_PACKAGE_HEADER);
  CopyMem (PackageListData, &PackageHeader, PackageHeader.Length);

  return PackageListHeader;
}

/**
  Assemble EFI_HII_PACKAGE_LIST according to the passed in packages.

  If GuidId is NULL, then ASSERT.
  If not enough resource to complete the operation, then ASSERT.

  @param  NumberOfPackages       Number of packages.
  @param  GuidId                 Package GUID.
  @param  ...                    Variable argument list for packages to be assembled.

  @return Pointer of EFI_HII_PACKAGE_LIST_HEADER.

**/
EFI_HII_PACKAGE_LIST_HEADER *
EFIAPI
HiiLibPreparePackageList (
  IN UINTN                    NumberOfPackages,
  IN CONST EFI_GUID           *GuidId,
  ...
  )
{
  EFI_HII_PACKAGE_LIST_HEADER *PackageListHeader;
  VA_LIST                     Marker;

  ASSERT (GuidId != NULL);

  VA_START (Marker, GuidId);
  PackageListHeader = InternalHiiLibPreparePackages (NumberOfPackages, GuidId, Marker);
  VA_END (Marker);

  return PackageListHeader;
}


/**
  This function allocates pool for an EFI_HII_PACKAGE_LIST structure
  with additional space that is big enough to host all packages described by the variable 
  argument list of package pointers.  The allocated structure is initialized using NumberOfPackages, 
  GuidId,  and the variable length argument list of package pointers.

  Then, EFI_HII_PACKAGE_LIST will be register to the default System HII Database. The
  Handle to the newly registered Package List is returned throught HiiHandle.

  If HiiHandle is NULL, then ASSERT.

  @param  NumberOfPackages    The number of HII packages to register.
  @param  GuidId              Package List GUID ID.
  @param  DriverHandle        Optional. If not NULL, the DriverHandle on which an instance of DEVICE_PATH_PROTOCOL is installed.
                              This DriverHandle uniquely defines the device that the added packages are associated with.
  @param  HiiHandle           On output, the HiiHandle is update with the handle which can be used to retrieve the Package 
                              List later. If the functions failed to add the package to the default HII database, this value will
                              be set to NULL.
  @param  ...                 The variable argument list describing all HII Package.

  @return  EFI_SUCCESS         If the packages are successfully added to the default HII database.
  @return  EFI_OUT_OF_RESOURCE Not enough resource to complete the operation.

**/
EFI_STATUS
EFIAPI
HiiLibAddPackages (
  IN       UINTN               NumberOfPackages,
  IN CONST EFI_GUID            *GuidId,
  IN       EFI_HANDLE          DriverHandle, OPTIONAL
  OUT      EFI_HII_HANDLE      *HiiHandle,
  ...
  )
{
  VA_LIST                   Args;
  EFI_HII_PACKAGE_LIST_HEADER *PackageListHeader;
  EFI_STATUS                Status;

  ASSERT (HiiHandle != NULL);

  LocateHiiProtocols ();

  VA_START (Args, HiiHandle);
  PackageListHeader = InternalHiiLibPreparePackages (NumberOfPackages, GuidId, Args);

  Status      = mHiiDatabaseProt->NewPackageList (mHiiDatabaseProt, PackageListHeader, DriverHandle, HiiHandle);
  if (HiiHandle != NULL) {
    if (EFI_ERROR (Status)) {
      *HiiHandle = NULL;
    }
  }

  FreePool (PackageListHeader);
  VA_END (Args);
  
  return Status;
}

/**
  Removes a package list from the default HII database.

  If HiiHandle is NULL, then ASSERT.
  If HiiHandle is not a valid EFI_HII_HANDLE in the default HII database, then ASSERT.

  @param  HiiHandle                The handle that was previously registered to the data base that is requested for removal.
                                             List later.

**/
VOID
EFIAPI
HiiLibRemovePackages (
  IN      EFI_HII_HANDLE      HiiHandle
  )
{
  EFI_STATUS Status;
  ASSERT (IsHiiHandleRegistered (HiiHandle));

  LocateHiiProtocols ();

  Status = mHiiDatabaseProt->RemovePackageList (mHiiDatabaseProt, HiiHandle);
  ASSERT_EFI_ERROR (Status);
}


/**
  Determines the handles that are currently active in the database.
  It's the caller's responsibility to free handle buffer.

  If HandleBufferLength is NULL, then ASSERT.
  If HiiHandleBuffer is NULL, then ASSERT.

  @param  HandleBufferLength     On input, a pointer to the length of the handle
                                 buffer. On output, the length of the handle buffer
                                 that is required for the handles found.
  @param  HiiHandleBuffer        Pointer to an array of Hii Handles returned.

  @retval EFI_SUCCESS            Get an array of Hii Handles successfully.

**/
EFI_STATUS
EFIAPI
HiiLibGetHiiHandles (
  IN OUT UINTN                     *HandleBufferLength,
  OUT    EFI_HII_HANDLE            **HiiHandleBuffer
  )
{
  UINTN       BufferLength;
  EFI_STATUS  Status;

  ASSERT (HandleBufferLength != NULL);
  ASSERT (HiiHandleBuffer != NULL);

  BufferLength = 0;

  LocateHiiProtocols ();

  //
  // Try to find the actual buffer size for HiiHandle Buffer.
  //
  Status = mHiiDatabaseProt->ListPackageLists (
                                 mHiiDatabaseProt,
                                 EFI_HII_PACKAGE_TYPE_ALL,
                                 NULL,
                                 &BufferLength,
                                 *HiiHandleBuffer
                                 );

  if (Status == EFI_BUFFER_TOO_SMALL) {
      *HiiHandleBuffer = AllocateZeroPool (BufferLength);
      ASSERT (*HiiHandleBuffer != NULL);
      Status = mHiiDatabaseProt->ListPackageLists (
                                     mHiiDatabaseProt,
                                     EFI_HII_PACKAGE_TYPE_ALL,
                                     NULL,
                                     &BufferLength,
                                     *HiiHandleBuffer
                                     );
      //
      // we should not fail here.
      //
      ASSERT_EFI_ERROR (Status);
  }

  *HandleBufferLength = BufferLength;

  return Status;
}

/**
  Extract Hii package list GUID for given HII handle.

  If HiiHandle could not be found in the default HII database, then ASSERT.
  If Guid is NULL, then ASSERT.

  @param  Handle              Hii handle
  @param  Guid                Package list GUID

  @retval EFI_SUCCESS            Successfully extract GUID from Hii database.

**/
EFI_STATUS
EFIAPI
HiiLibExtractGuidFromHiiHandle (
  IN      EFI_HII_HANDLE      Handle,
  OUT     EFI_GUID            *Guid
  )
{
  EFI_STATUS                   Status;
  UINTN                        BufferSize;
  EFI_HII_PACKAGE_LIST_HEADER  *HiiPackageList;

  ASSERT (Guid != NULL);
  ASSERT (IsHiiHandleRegistered (Handle));

  //
  // Get HII PackageList
  //
  BufferSize = 0;
  HiiPackageList = NULL;

  LocateHiiProtocols ();

  Status = mHiiDatabaseProt->ExportPackageLists (mHiiDatabaseProt, Handle, &BufferSize, HiiPackageList);
  ASSERT (Status != EFI_NOT_FOUND);
  
  if (Status == EFI_BUFFER_TOO_SMALL) {
    HiiPackageList = AllocatePool (BufferSize);
    ASSERT (HiiPackageList != NULL);

    Status = mHiiDatabaseProt->ExportPackageLists (mHiiDatabaseProt, Handle, &BufferSize, HiiPackageList);
  }
  if (EFI_ERROR (Status)) {
    FreePool (HiiPackageList);
    return Status;
  }

  //
  // Extract GUID
  //
  CopyGuid (Guid, &HiiPackageList->PackageListGuid);

  FreePool (HiiPackageList);

  return EFI_SUCCESS;
}

/**
  Find HII Handle in the default HII database associated with given Device Path.

  If DevicePath is NULL, then ASSERT.

  @param  DevicePath             Device Path associated with the HII package list
                                 handle.

  @retval Handle                 HII package list Handle associated with the Device
                                        Path.
  @retval NULL                   Hii Package list handle is not found.

**/
EFI_HII_HANDLE
EFIAPI
HiiLibDevicePathToHiiHandle (
  IN EFI_DEVICE_PATH_PROTOCOL   *DevicePath
  )
{
  EFI_STATUS                  Status;
  EFI_DEVICE_PATH_PROTOCOL    *TmpDevicePath;
  UINTN                       BufferSize;
  UINTN                       HandleCount;
  UINTN                       Index;
  EFI_HANDLE                  *Handles;
  EFI_HANDLE                  Handle;
  UINTN                       Size;
  EFI_HANDLE                  DriverHandle;
  EFI_HII_HANDLE              *HiiHandles;
  EFI_HII_HANDLE              HiiHandle;

  ASSERT (DevicePath != NULL);

  //
  // Locate Device Path Protocol handle buffer
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiDevicePathProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  //
  // Search Driver Handle by Device Path
  //
  DriverHandle = NULL;
  BufferSize = GetDevicePathSize (DevicePath);
  for(Index = 0; Index < HandleCount; Index++) {
    Handle = Handles[Index];
    gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **) &TmpDevicePath);

    //
    // Check whether DevicePath match
    //
    Size = GetDevicePathSize (TmpDevicePath);
    if ((Size == BufferSize) && CompareMem (DevicePath, TmpDevicePath, Size) == 0) {
      DriverHandle = Handle;
      break;
    }
  }
  FreePool (Handles);

  if (DriverHandle == NULL) {
    return NULL;
  }

  LocateHiiProtocols ();

  //
  // Retrieve all Hii Handles from HII database
  //
  BufferSize = 0x1000;
  HiiHandles = AllocatePool (BufferSize);
  ASSERT (HiiHandles != NULL);
  Status = mHiiDatabaseProt->ListPackageLists (
                          mHiiDatabaseProt,
                          EFI_HII_PACKAGE_TYPE_ALL,
                          NULL,
                          &BufferSize,
                          HiiHandles
                          );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    FreePool (HiiHandles);
    HiiHandles = AllocatePool (BufferSize);
    ASSERT (HiiHandles != NULL);

    Status = mHiiDatabaseProt->ListPackageLists (
                            mHiiDatabaseProt,
                            EFI_HII_PACKAGE_TYPE_ALL,
                            NULL,
                            &BufferSize,
                            HiiHandles
                            );
  }

  if (EFI_ERROR (Status)) {
    FreePool (HiiHandles);
    return NULL;
  }

  //
  // Search Hii Handle by Driver Handle
  //
  HiiHandle = NULL;
  HandleCount = BufferSize / sizeof (EFI_HII_HANDLE);
  for (Index = 0; Index < HandleCount; Index++) {
    Status = mHiiDatabaseProt->GetPackageListHandle (
                            mHiiDatabaseProt,
                            HiiHandles[Index],
                            &Handle
                            );
    if (!EFI_ERROR (Status) && (Handle == DriverHandle)) {
      HiiHandle = HiiHandles[Index];
      break;
    }
  }

  FreePool (HiiHandles);
  return HiiHandle;
}

/**
  Exports the contents of one or all package lists in the HII database into a buffer.

  If Handle is not NULL and not a valid EFI_HII_HANDLE registered in the database, 
  then ASSERT.
  If PackageListHeader is NULL, then ASSERT.
  If PackageListSize is NULL, then ASSERT.

  @param  Handle                 The HII Handle.
  @param  PackageListHeader      A pointer to a buffer that will contain the results of 
                                 the export function.
  @param  PackageListSize        On output, the length of the buffer that is required for the exported data.

  @retval EFI_SUCCESS            Package exported.

  @retval EFI_OUT_OF_RESOURCES   Not enought memory to complete the operations.

**/
EFI_STATUS 
EFIAPI
HiiLibExportPackageLists (
  IN EFI_HII_HANDLE                    Handle,
  OUT EFI_HII_PACKAGE_LIST_HEADER      **PackageListHeader,
  OUT UINTN                            *PackageListSize
  )
{
  EFI_STATUS                       Status;
  UINTN                            Size;
  EFI_HII_PACKAGE_LIST_HEADER      *PackageListHdr;

  ASSERT (PackageListSize != NULL);
  ASSERT (PackageListHeader != NULL);

  LocateHiiProtocols ();

  if (Handle != NULL) {
    ASSERT (IsHiiHandleRegistered (Handle));
  }

  Size = 0;
  PackageListHdr = NULL;
  Status = mHiiDatabaseProt->ExportPackageLists (
                                      mHiiDatabaseProt,
                                      Handle,
                                      &Size,
                                      PackageListHdr
                                      );
  ASSERT_EFI_ERROR (Status != EFI_BUFFER_TOO_SMALL);
  
  if (Status == EFI_BUFFER_TOO_SMALL) {
    PackageListHdr = AllocateZeroPool (Size);
    
    if (PackageListHeader == NULL) {
      return EFI_OUT_OF_RESOURCES;
    } else {
      Status = mHiiDatabaseProt->ExportPackageLists (
                                          mHiiDatabaseProt,
                                          Handle,
                                          &Size,
                                          PackageListHdr
                                           );
    }
  }

  if (!EFI_ERROR (Status)) {
    *PackageListHeader = PackageListHdr;
    *PackageListSize   = Size;
  } else {
    FreePool (PackageListHdr);
  }

  return Status;
}


EFI_STATUS
EFIAPI
HiiLibListPackageLists (
  IN        UINT8                     PackageType,
  IN CONST  EFI_GUID                  *PackageGuid,
  IN OUT    UINTN                     *HandleBufferLength,
  OUT       EFI_HII_HANDLE            **HandleBuffer
  )
{
  EFI_STATUS          Status;
  
  ASSERT (HandleBufferLength != NULL);
  ASSERT (HandleBuffer != NULL);
  
  *HandleBufferLength = 0;
  *HandleBuffer       = NULL;

  LocateHiiProtocols ();

  Status = mHiiDatabaseProt->ListPackageLists (
                            mHiiDatabaseProt,
                            PackageType,
                            PackageGuid,
                            HandleBufferLength,
                            *HandleBuffer
                            );
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    //
    // No packages is registered to UEFI HII Database, just return EFI_SUCCESS.
    // 
    //
    return Status;
  }

  *HandleBuffer = AllocateZeroPool (*HandleBufferLength);
  
  if (*HandleBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  
  return mHiiDatabaseProt->ListPackageLists (
                            mHiiDatabaseProt,
                            PackageType,
                            PackageGuid,
                            HandleBufferLength,
                            *HandleBuffer
                            );
  
}
/**
  This function check if the Hii Handle is a valid handle registered
  in the HII database.

  @param HiiHandle The HII Handle.

  @retval TRUE If it is a valid HII handle.
  @retval FALSE If it is a invalid HII handle.
**/
BOOLEAN
IsHiiHandleRegistered (
  EFI_HII_HANDLE    HiiHandle
  )
{
  EFI_STATUS                   Status;
  UINTN                        BufferSize;
  EFI_HII_PACKAGE_LIST_HEADER  *HiiPackageList;

  ASSERT (HiiHandle != NULL);

  HiiPackageList = NULL;
  BufferSize = 0;

  LocateHiiProtocols ();

  Status = mHiiDatabaseProt->ExportPackageLists (
             mHiiDatabaseProt,
             HiiHandle,
             &BufferSize,
             HiiPackageList
             );

  return (BOOLEAN) (Status == EFI_BUFFER_TOO_SMALL);
}


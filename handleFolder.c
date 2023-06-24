/*
 * Copyright (c) 2001-2020 Apple Inc. All Rights Reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *  handleFolder.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Thu Dec 6 2001.
 *  Copyright (c) 2001-2007 Apple Inc. All Rights Reserved.
 *
 *  $Id: handleFolder.c,v 1.79 2006/07/21 14:59:24 ssen Exp $
 *
 */

#include <CoreFoundation/CoreFoundation.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <paths.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>


#include <APFS/APFS.h>

#include "enums.h"
#include "structs.h"

#include "bless.h"
#include "bless_private.h"
#include "protos.h"


static int isOFLabel(const char *data, int labelsize);

int extractMountPoint(BLContextPtr context, struct clarg actargs[klast]) {
	int ret;
	
	if(actargs[kmount].present) {
		ret = BLGetCommonMountPoint(context, actargs[kmount].argument, "", actargs[kmount].argument);
		if(ret) {
			blesscontextprintf(context, kBLLogLevelError,  "Can't determine mount point of '%s'\n", actargs[kmount].argument );
		} else {
			blesscontextprintf(context, kBLLogLevelVerbose,  "Mount point is '%s'\n", actargs[kmount].argument );
		}
	} else if(actargs[kfolder].present) {
		/* We know that at least one folder has been specified */
		ret = BLGetCommonMountPoint(context, actargs[kfolder].argument, "", actargs[kmount].argument);
		if(ret) {
			blesscontextprintf(context, kBLLogLevelError, "Can't determine mount point of '%s'\n",
							   actargs[kfolder].argument);
			return 1;
		} else {
			actargs[kmount].present = true;
			blesscontextprintf(context, kBLLogLevelVerbose, "Common mount point of '%s' is %s\n",
							   actargs[kfolder].argument,
							   actargs[kmount].argument);
		}
	} else {
		blesscontextprintf(context, kBLLogLevelError, "No volume specified\n" );
		return 1;
	}
	
	return ret;
}

int modeFolder(BLContextPtr context, struct clarg actargs[klast]) {
	
    int ret;
    int isHFS, isAPFS, shouldBless;
    bool isAPFSDataRolePreSSVToSSVThusDontWriteToVol;
	
    uint32_t folderXid = 0;                   // The directory ID specified by folderXpath
    
	CFDataRef bootEFIdata = NULL;
	CFDataRef labeldata = NULL;
	CFDataRef labeldata2 = NULL;
	struct statfs sb;
	BLVersionRec	osVersion;
    uint16_t role = APFS_VOL_ROLE_NONE;
    bool     isARV = false;
	BLPreBootEnvType	preboot = getPrebootType();
	bool				useFullPath = false;

	if (extractMountPoint(context, actargs) != 0) {
		blesscontextprintf(context, kBLLogLevelError,  "Could not extract mount point\n");
		return 1;
	}
	
	/*
	 * actargs[kmount].argument will always be filled in as the volume we are
	 * operating. Look at actargs[kfolder].present to see if the user wanted
	 * to bless something specifically, or just wanted to use -setBoot
	 * or something
	 */
	if( actargs[kfolder].present) {
		shouldBless = 1;
	} else {
		shouldBless = 0;
	}
	
	if(shouldBless) {
		// if we're blessing the volume, we need something for
		// finderinfo[1]. If you didn't provide a file, but we're
		// planning on generating one, fill in the path now
		
		if(!actargs[kfile].present && actargs[kbootefi].present) {
            // you didn't give a booter file explicitly, so we have to guess
            // based on the system folder.
            snprintf(actargs[kfile].argument, sizeof(actargs[kfile].argument),
                     "%s/boot.efi", actargs[kfolder].argument);
            actargs[kfile].present = 1;
        }        
    }
    
    ret = BLIsMountHFS(context, actargs[kmount].argument, &isHFS);
    if (ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Could not determine filesystem of %s\n", actargs[kmount].argument );
        return 1;
    }
    
    ret = BLIsMountAPFS(context, actargs[kmount].argument, &isAPFS);
    if (ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Could not determine filesystem of %s\n", actargs[kmount].argument );
        return 1;
    }
    
    ret = BLIsMountAPFSDataRolePreSSVToSSV(context, actargs[kmount].argument, &isAPFSDataRolePreSSVToSSVThusDontWriteToVol);
    if (ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Could not determine SSV status of %s\n", actargs[kmount].argument );
        return 1;
    }
    
	if (0 != blsustatfs(actargs[kmount].argument, &sb)) {
		blesscontextprintf(context, kBLLogLevelError,  "Can't statfs %s\n" ,
						   actargs[kmount].argument);
		return 1;
	}
    
    if (isAPFS) {
        if (APFSVolumeRole(sb.f_mntfromname + 5, &role, NULL)) {
            blesscontextprintf(context, kBLLogLevelError, "Couldn't get role for volume %s\n", actargs[kmount].argument);
            return 2;
        }
    
        if (BLIsVolumeARV(context, sb.f_mntonname, sb.f_mntfromname + strlen(_PATH_DEV), &isARV)) {
            blesscontextprintf(context, kBLLogLevelError, "Couldn't check if volume %s is ARV\n", actargs[kmount].argument);
            return 2;
        }
    }

    /* If user gave options that require boot.efi creation, do it now. */
    if(actargs[kbootefi].present) {
        if(!actargs[kbootefi].hasArg) {
			// Let's see what OS we're on
			BLGetOSVersion(context, actargs[kmount].argument, &osVersion);
			if (osVersion.major < 10) {
				// Uh-oh, what do we do with this?
				blesscontextprintf(context, kBLLogLevelError, "OS Major version unrecognized\n");
				return 2;
			}
			actargs[kbootefi].argument[0] = '\0';
			if (osVersion.major > 10 || osVersion.minor >= 11) {	// v10.11 and later
				// use bootdev.efi by default if it exists
				snprintf(actargs[kbootefi].argument, kMaxArgLength-1, "%s%s", actargs[kmount].argument, kBL_PATH_I386_BOOTDEV_EFI);
				if (access(actargs[kbootefi].argument, R_OK) != 0) {
					actargs[kbootefi].argument[0] = '\0';
				}
			}
			if (!actargs[kbootefi].argument[0]) {
				snprintf(actargs[kbootefi].argument, kMaxArgLength-1, "%s%s", actargs[kmount].argument, kBL_PATH_I386_BOOT_EFI);
			}
        }
		
		if (!isAPFS || !(sb.f_flags & MNT_RDONLY)) {
		
			ret = BLLoadFile(context, actargs[kbootefi].argument, 0, &bootEFIdata);
			if (ret) {
				blesscontextprintf(context, kBLLogLevelVerbose,  "Could not load boot.efi data from %s\n",
								   actargs[kbootefi].argument);
			}
			
			if (actargs[kfile].present && bootEFIdata) {
				
				// check to see if needed
				CFDataRef oldEFIdata = NULL;
				struct stat oldPresence;
				
				ret = lstat(actargs[kfile].argument, &oldPresence);
				
				if (ret == 0 && S_ISREG(oldPresence.st_mode)) {
					ret = BLLoadFile(context, actargs[kfile].argument, 0, &oldEFIdata);
				}
				if ((ret == 0) && oldEFIdata && CFEqual(oldEFIdata, bootEFIdata)) {
					blesscontextprintf(context, kBLLogLevelVerbose,  "boot.efi unchanged at %s. Skipping update...\n",
									   actargs[kfile].argument );
				} else {
					ret = BLCreateFileWithOptions(context, bootEFIdata, actargs[kfile].argument, 0, 0, 0, isAPFS ? kTryPreallocate : kMustPreallocate);
					if (ret) {
						blesscontextprintf(context, kBLLogLevelError,  "Could not create boot.efi at %s\n", actargs[kfile].argument );
						return 2;
					} else {
						blesscontextprintf(context, kBLLogLevelVerbose,  "boot.efi created successfully at %s\n",
										   actargs[kfile].argument );
					}
				}
				
				if (oldEFIdata) CFRelease(oldEFIdata);
				ret = CopyManifests(context, actargs[kfile].argument, actargs[kbootefi].argument, actargs[kbootefi].argument);
				if (ret) {
					blesscontextprintf(context, kBLLogLevelError, "Can't copy img4 manifests for file %s\n", actargs[kfile].argument);
					return 3;
				}
			} else {
				blesscontextprintf(context, kBLLogLevelVerbose,  "Could not create boot.efi, no X folder specified\n" );
			}
		}
    }
	
	if (isAPFS && !isAPFSDataRolePreSSVToSSVThusDontWriteToVol && actargs[ksetboot].present) {
		char        pathBuf[MAXPATHLEN];
		CFDataRef   apfsDriverData;
		char        wholeDiskBSD[1024];
		int         unit;
		bool        attemptLoad = false;
		const char  *path;
		
		// We need to embed the APFS driver in the container.
		sscanf(sb.f_mntfromname + 5, "disk%d", &unit);
		snprintf(wholeDiskBSD, sizeof wholeDiskBSD, "disk%d", unit);
		if (!actargs[kapfsdriver].present) {
			if (!actargs[knoapfsdriver].present) {
				snprintf(pathBuf, sizeof pathBuf, "%s%s", actargs[kmount].argument, kBL_PATH_I386_APFS_EFI);
				attemptLoad = true;
				ret = BLLoadFile(context, pathBuf, 0, &apfsDriverData);
				path = pathBuf;
			}
		} else {
			attemptLoad = true;
			ret = BLLoadFile(context, actargs[kapfsdriver].argument, 0, &apfsDriverData);
			path = actargs[kapfsdriver].argument;
		}
		if (attemptLoad) {
			if (ret) {
				blesscontextprintf(context, kBLLogLevelError,  "Could not load apfs.efi data from %s: %d\n",
								   path, ret);
				return 1;
			}
			ret = APFSContainerEFIEmbed(wholeDiskBSD, (const char *)CFDataGetBytePtr(apfsDriverData),
										(uint32_t)CFDataGetLength(apfsDriverData));
			if (ret) {
				blesscontextprintf(context, kBLLogLevelError, "Could not embed APFS driver in %s - error #%d\n",
								   wholeDiskBSD, ret);
				return 1;
			}
			CFRelease(apfsDriverData);
		}
	}

	
	if (!isAPFSDataRolePreSSVToSSVThusDontWriteToVol && (actargs[klabel].present || actargs[klabelfile].present)) {
		int isLabel = 0;
		
		if(actargs[klabelfile].present) {
			ret = BLLoadFile(context, actargs[klabelfile].argument, 0, &labeldata);
			if(ret) {
				blesscontextprintf(context, kBLLogLevelError, "Can't load label '%s'\n",
								   actargs[klabelfile].argument);
				return 2;
			}
		} else {
			ret = BLGenerateLabelData(context, actargs[klabel].argument, kBitmapScale_1x, &labeldata);
			if (ret) {
				blesscontextprintf(context, kBLLogLevelError, "Can't render label '%s'\n",
								   actargs[klabel].argument);
				return 3;
			}
			ret = BLGenerateLabelData(context, actargs[klabel].argument, kBitmapScale_2x, &labeldata2);
			if (ret) {
				blesscontextprintf(context, kBLLogLevelError, "Can't render label '%s'\n",
								   actargs[klabel].argument);
				return 3;
			}
		}
		
		isLabel = isOFLabel((const char *)CFDataGetBytePtr(labeldata), (int)CFDataGetLength(labeldata));
		blesscontextprintf(context, kBLLogLevelVerbose,  "Scale 1 label data is valid: %s\n",
						   isLabel ? "YES" : "NO");
		
		if (actargs[kfolder].present && (!isAPFS || !(sb.f_flags & MNT_RDONLY))) {
			char sysfolder[MAXPATHLEN];
			
			snprintf(sysfolder, sizeof sysfolder, "%s/.disk_label", actargs[kfolder].argument);
			ret = WriteLabelFile(context, sysfolder, labeldata, isLabel && isHFS, kBitmapScale_1x);
			if (ret) return 1;
			
			if (labeldata2) {
				snprintf(sysfolder, sizeof sysfolder, "%s/.disk_label_2x", actargs[kfolder].argument);
				ret = WriteLabelFile(context, sysfolder, labeldata2, 0, kBitmapScale_2x);
				if (ret) return 1;
			}
		}
	}
	

    if (shouldBless || (isAPFS && !isAPFSDataRolePreSSVToSSVThusDontWriteToVol)) {
        if (isHFS) {
            uint32_t oldwords[8];
            uint32_t bootfile = 0;
            
            ret = BLGetVolumeFinderInfo(context, actargs[kmount].argument, oldwords);
            if(ret) {
                blesscontextprintf(context, kBLLogLevelError,  "Error getting old Finder info words for %s\n", actargs[kmount].argument );
                return 1;
            }
            
            /* always save boot file */
            bootfile = oldwords[1];
            
            /* bless! bless */
            
            /* First get any directory IDs we need */
            if(actargs[kfolder].present) {
                ret = BLGetFileID(context, actargs[kfolder].argument, &folderXid);
                if(ret) {
                    blesscontextprintf(context, kBLLogLevelError,  "Error while getting directory ID of %s\n", actargs[kfolder].argument );
                } else {
                    blesscontextprintf(context, kBLLogLevelVerbose,  "Got directory ID of %u for %s\n",
                                       folderXid, actargs[kfolder].argument );
                }
            }
            
            if(actargs[kfile].present) {
                ret = BLGetFileID(context, actargs[kfile].argument, &bootfile);
                if(ret) {
                    blesscontextprintf(context, kBLLogLevelError,  "Error while getting file ID of %s. Ignoring...\n", actargs[kfile].argument );
                    bootfile = 0;
                } else {
                    struct stat checkForFile;
                    
                    ret = lstat(actargs[kfile].argument, &checkForFile);
                    if(ret || !S_ISREG(checkForFile.st_mode)) {
                        blesscontextprintf(context, kBLLogLevelError,  "%s cannot be accessed, or is not a regular file. Ignoring...\n", actargs[kfile].argument );
                        bootfile = 0;
                    } else {
                        blesscontextprintf(context, kBLLogLevelVerbose,  "Got file ID of %u for %s\n",
                                           bootfile, actargs[kfile].argument );
                    }
                }
                
            } else {
                // no file given. we should try to verify the existing booter
                if(bootfile) {
                    ret = BLLookupFileIDOnMount(context, actargs[kmount].argument, bootfile, actargs[kfile].argument);
                    if(ret) {
                        blesscontextprintf(context, kBLLogLevelVerbose,  "Invalid EFI blessed file ID %u. Zeroing...\n",
                                           bootfile );
                        bootfile = 0;
                    } else {
                        struct stat checkForFile;
                        
                        ret = lstat(actargs[kfile].argument, &checkForFile);
                        if(ret || !S_ISREG(checkForFile.st_mode)) {
                            blesscontextprintf(context, kBLLogLevelError,  "%s cannot be accessed, or is not a regular file. Ignoring...\n", actargs[kfile].argument );
                            bootfile = 0;
                        } else {
                            blesscontextprintf(context, kBLLogLevelVerbose,
                                               "Preserving EFI blessed file ID %u for %s\n",
                                               bootfile, actargs[kfile].argument );
                        }
                    }
                }
            }
            
            /* If either directory was not specified, the dirID
             * variables will be 0, so we can use that to initialize
             * the FI fields */
            
            /* Set Finder info words 1 & 5*/
            oldwords[1] = bootfile;
            oldwords[5] = folderXid;
            
            // reserved1 returns the f_fssubtype attribute. Right now, 0 == HFS+,
            // 1 == HFS+J. Anything else is either HFS plain, or some form
            // of HFSX. These filesystems we don't want blessed, because we don't
            // want future versions of OF to list them as bootable, but rather
            // prefer the Apple_Boot partition
            //
            // For EFI-based systems, it's OK to set finderinfo[0], and indeed
            // a better user experience so that the EFI label shows up
            
            if(actargs[ksetboot].present &&
               (preboot == kBLPreBootEnvType_OpenFirmware) &&
#if _DARWIN_FEATURE_64_BIT_INODE
               (sb.f_fssubtype & ~1)
#else
               (sb.f_reserved1 & ~1)
#endif
               ) {
                blesscontextprintf(context, kBLLogLevelVerbose,  "%s is not HFS+ or Journaled HFS+. Not setting finderinfo[0]...\n", actargs[kmount].argument );
                oldwords[0] = 0;
            } else {
                if(folderXid) {
                    oldwords[0] = folderXid;
                }
            }
            
            blesscontextprintf(context, kBLLogLevelVerbose,  "finderinfo[0] = %d\n", oldwords[0] );
            blesscontextprintf(context, kBLLogLevelVerbose,  "finderinfo[1] = %d\n", oldwords[1] );
            blesscontextprintf(context, kBLLogLevelVerbose,  "finderinfo[5] = %d\n", oldwords[5] );
            
            
            if(geteuid() != 0 && geteuid() != sb.f_owner) {
                blesscontextprintf(context, kBLLogLevelError,  "Authorization required\n" );
                return 1;
            }
            
            ret = BLSetVolumeFinderInfo(context,  actargs[kmount].argument, oldwords);
            if(ret) {
                blesscontextprintf(context, kBLLogLevelError,  "Can't set Finder info fields for volume mounted at %s: %s\n", actargs[kmount].argument , strerror(errno));
                return 2;
            }
            
        } else if (isAPFS) {
            uint64_t oldWords[2] = { 0, 0 };
            uint64_t folderInum = 0;
            uint64_t fileInum = 0;
            char     *bootEFISource;
            
            if (shouldBless) {
                if (role == APFS_VOL_ROLE_PREBOOT || role == APFS_VOL_ROLE_RECOVERY) {
					if (actargs[kfile].present) useFullPath = true;
					
                    ret = BLGetAPFSBlessData(context, actargs[kmount].argument, oldWords);
                    if (ret && ret != ENOENT) {
                        blesscontextprintf(context, kBLLogLevelError,  "Error getting bless data for %s\n", actargs[kmount].argument);
                        return 1;
                    }
                    
                    /* bless! bless */
                    
                    /* First get any directory IDs we need */
                    if (actargs[kfolder].present) {
                        ret = BLGetAPFSInodeNum(context, actargs[kfolder].argument, &folderInum);
                        if (ret) {
                            blesscontextprintf(context, kBLLogLevelError,  "Error while getting inum of %s\n", actargs[kfolder].argument);
                        } else {
                            blesscontextprintf(context, kBLLogLevelVerbose,  "Got inum of %llu for %s\n",
                                               folderInum, actargs[kfolder].argument );
                        }
                    }
                    
                    if (actargs[kfile].present) {
                        ret = BLGetAPFSInodeNum(context, actargs[kfile].argument, &fileInum);
                        if (ret) {
                            blesscontextprintf(context, kBLLogLevelError,  "Error while getting inum of %s. Ignoring...\n", actargs[kfile].argument );
                            fileInum = 0;
                        } else {
                            struct stat checkForFile;
                            
                            ret = lstat(actargs[kfile].argument, &checkForFile);
                            if (ret || !S_ISREG(checkForFile.st_mode)) {
                                blesscontextprintf(context, kBLLogLevelError,  "%s cannot be accessed, or is not a regular file. Ignoring...\n", actargs[kfile].argument );
                                fileInum = 0;
                            } else {
                                blesscontextprintf(context, kBLLogLevelVerbose,  "Got inum of %llu for %s\n",
                                                   fileInum, actargs[kfile].argument);
                            }
                        }
                        
                    } else {
                        // no file given. we should try to verify the existing booter
                        if (oldWords[0]) {
                            ret = BLLookupFileIDOnMount64(context, actargs[kmount].argument, oldWords[0], actargs[kfile].argument, sizeof actargs[kfile].argument);
                            if (ret) {
                                blesscontextprintf(context, kBLLogLevelVerbose,  "Invalid EFI blessed file ID %llu. Zeroing...\n",
                                                   oldWords[0] );
                                oldWords[0] = 0;
                            } else {
                                struct stat checkForFile;
                                
                                ret = lstat(actargs[kfile].argument, &checkForFile);
                                if(ret || !S_ISREG(checkForFile.st_mode)) {
                                    blesscontextprintf(context, kBLLogLevelError,  "%s cannot be accessed, or is not a regular file. Ignoring...\n", actargs[kfile].argument );
                                    oldWords[0] = 0;
                                } else {
                                    blesscontextprintf(context, kBLLogLevelVerbose,
                                                       "Preserving EFI blessed file ID %llu for %s\n",
                                                       oldWords[0], actargs[kfile].argument );
									fileInum = oldWords[0];
                                }
                            }
                        }
                        
                    }
                    
                    oldWords[0] = fileInum;
                    oldWords[1] = folderInum;
                    
                    blesscontextprintf(context, kBLLogLevelVerbose,  "blessed file = %lld\n", oldWords[0]);
                    blesscontextprintf(context, kBLLogLevelVerbose,  "blessed folder = %lld\n", oldWords[1]);
					
					if (geteuid() != 0 && geteuid() != sb.f_owner) {
						blesscontextprintf(context, kBLLogLevelError,  "Authorization required\n" );
						return 1;
					}
					
					ret = BLSetAPFSBlessData(context,  actargs[kmount].argument, oldWords);
					if (ret) {
						blesscontextprintf(context, kBLLogLevelError,  "Can't set bless data for volume mounted at %s: %s\n", actargs[kmount].argument , strerror(errno));
						return 2;
					}
					
                }
            }


            bootEFISource =  actargs[kbootefi].present ? actargs[kbootefi].argument : NULL;
            
            // If there is no argument for --bootefi, use the default boot efi path
            if((bootEFISource != NULL) && (!actargs[kbootefi].hasArg)) {
                bootEFISource = bootEFISource + strlen(actargs[kmount].argument);
            }
            ret = BlessPrebootVolume(context, sb.f_mntfromname + 5, bootEFISource, labeldata, labeldata2,
                                     true, actargs);
            if (ret) {
                blesscontextprintf(context, kBLLogLevelError,  "Couldn't bless the APFS preboot volume for volume mounted at %s: %s\n",
                                   actargs[kmount].argument, strerror(errno));
                return 2;
            }
        }
    }
	
	if (actargs[kpersonalize].present) {
        // If --allowUI was passed, then don't suppress the UI prompt for AppleConnect.
        // This is internal-only, so it's not mentioned in the man page.
		ret = PersonalizeOSVolume(context, actargs[kmount].argument, NULL, !actargs[kallowui].present);
		if (ret) {
			blesscontextprintf(context, kBLLogLevelError, "Couldn't personalize volume %s\n", actargs[kmount].argument);
			return ret;
		}
	}


    /* Set Open Firmware to boot off the specified volume*/
    if(actargs[ksetboot].present) {
        if(preboot == kBLPreBootEnvType_EFI) {
			// if you blessed the volume, then just point EFI at the volume.
			// only if you didn't bless, but you have something interesting
			// to point at, should you use actargs[kfile]
            

            if (actargs[klegacy].present) {
                ret = setefilegacypath(context, actargs[kmount].argument, actargs[knextonly].present,
									   actargs[klegacydrivehint].present ? actargs[klegacydrivehint].argument : NULL,
									   actargs[koptions].present ? actargs[koptions].argument : NULL);

            } else {
				if (!shouldBless && !isAPFS && actargs[kfile].present) useFullPath = true;
                ret = setefifilepath(context, (useFullPath ? actargs[kfile].argument : actargs[kmount].argument),
                                 actargs[knextonly].present,
                                 actargs[koptions].present ? actargs[koptions].argument : NULL,
                                 actargs[kshortform].present ? true : false);
            }
            if(ret) {
                return 3;
            }
        } else {
            struct statfs sb;
            
            ret = blsustatfs(actargs[kmount].argument, &sb);
            if(ret) {
                blesscontextprintf(context, kBLLogLevelError,  "Can't statfs: %s\n",
                                   strerror(errno));			
                return 2;
            }
            
            ret = setboot(context, sb.f_mntfromname, NULL, labeldata);
            if(ret) {
                return 3;
            }
        }		
    }
	
	if (bootEFIdata) CFRelease(bootEFIdata);
	if (labeldata) CFRelease(labeldata);
	if (labeldata2) CFRelease(labeldata2);

	
	return 0;
}

static int isOFLabel(const char *data, int labelsize)
{
    uint16_t width, height;
    
    if(data[0] != 1) return 0;

    width = CFSwapInt16BigToHost(*(uint16_t *)&data[1]);
    height = CFSwapInt16BigToHost(*(uint16_t *)&data[3]);

    if(labelsize != (width*height+5)) return 0;

    // basic sanity checks for version and dimensions were satisfied
    return 1;
}

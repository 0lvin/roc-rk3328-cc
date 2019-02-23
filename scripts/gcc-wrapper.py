#! /usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of The Linux Foundation nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Invoke gcc, looking for warnings, and causing a failure if there are
# non-whitelisted warnings.

import errno
import re
import os
import sys
import subprocess

# Note that gcc uses unicode, which may depend on the locale.  TODO:
# force LANG to be set to en_US.UTF-8 to get consistent warnings.

allowed_warnings = set([
    "posix-cpu-timers.c:1268", # kernel/time/posix-cpu-timers.c:1268:13: warning: 'now' may be used uninitialized in this function
    "af_unix.c:1036", # net/unix/af_unix.c:1036:20: warning: 'hash' may be used uninitialized in this function
    "sunxi_sram.c:214", # drivers/soc/sunxi/sunxi_sram.c:214:24: warning: 'device' may be used uninitialized in this function
    "ks8851.c:298", # drivers/net/ethernet/micrel/ks8851.c:298:2: warning: 'rxb[0]' may be used uninitialized in this function
    "ks8851.c:421", # drivers/net/ethernet/micrel/ks8851.c:421:20: warning: 'rxb[0]' may be used uninitialized in this function
    "compat_binfmt_elf.c:58", # fs/compat_binfmt_elf.c:58:13: warning: 'cputime_to_compat_timeval' defined but not used
    "memcontrol.c:5337", # mm/memcontrol.c:5337:12: warning: initialization from incompatible pointer type
    "atags_to_fdt.c:98", # arch/arm/boot/compressed/atags_to_fdt.c:98:1: warning: the frame size of 1032 bytes is larger than 1024 bytes
    "drm_edid.c:3506", # drivers/gpu/drm/drm_edid.c:3506:13: warning: 'cea_db_is_hdmi_forum_vsdb' defined but not used
    "vdso.c:120", # arch/arm64/kernel/vdso.c:119:6: warning: ‘memcmp’ reading 4 bytes from a region of size 1 [-Wstringop-overflow=]
    "syscalls.h:195", # include/linux/syscalls.h:195:18: warning: ‘sys_set_tid_address’ alias between functions of incompatible types ‘long int(int *)’ and ‘long int(long int)’ [-Wattribute-alias]
    "compat.h:48", # include/linux/compat.h:48:18: warning: ‘compat_sys_sysctl’ alias between functions of incompatible types ‘long int(struct compat_sysctl_args *)’ and ‘long int(long int)’ [-Wattribute-alias]
    "regcache-rbtree.c:36", # drivers/base/regmap/regcache-rbtree.c:36:1: warning: alignment 1 of ‘struct regcache_rbtree_node’ is less than 8 [-Wpacked-not-aligned]
    "dm.c:2488", # drivers/net/wireless/realtek/rtlwifi/rtl8821ae/dm.c:2488:3: warning: this ‘for’ clause does not guard... [-Wmisleading-indentation]
    "scsi.c:555", # drivers/nvme/host/scsi.c:555:2: warning: ‘strncpy’ output truncated before terminating nul copying 8 bytes from a string of the same length [-Wstringop-truncation]
    "sctp.h:331", # include/uapi/linux/sctp.h:331:1: warning: alignment 4 of ‘struct sctp_paddr_change’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:605", # include/uapi/linux/sctp.h:580:1: warning: alignment 4 of ‘struct sctp_setpeerprim’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:604", # include/uapi/linux/sctp.h:579:26: warning: ‘sspp_addr’ offset 4 in ‘struct sctp_setpeerprim’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "sctp.h:618", # include/uapi/linux/sctp.h:593:1: warning: alignment 4 of ‘struct sctp_prim’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:617", # include/uapi/linux/sctp.h:592:26: warning: ‘ssp_addr’ offset 4 in ‘struct sctp_prim’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "sctp.h:665", # include/uapi/linux/sctp.h:640:1: warning: alignment 4 of ‘struct sctp_paddrparams’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:659", # include/uapi/linux/sctp.h:634:26: warning: ‘spp_address’ offset 4 in ‘struct sctp_paddrparams’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "sctp.h:772", # include/uapi/linux/sctp.h:747:1: warning: alignment 4 of ‘struct sctp_paddrinfo’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:766", # include/uapi/linux/sctp.h:741:26: warning: ‘spinfo_address’ offset 4 in ‘struct sctp_paddrinfo’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:532", # net/compat.c:532:35: warning: ‘gf_group’ offset 4 in ‘struct compat_group_filter’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:534", # net/compat.c:534:1: warning: alignment 4 of ‘struct compat_group_req’ is less than 8 [-Wpacked-not-aligned]
    "compat.c:538", # net/compat.c:522:35: warning: ‘gsr_group’ offset 4 in ‘struct compat_group_source_req’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:540", # net/compat.c:524:35: warning: ‘gsr_source’ offset 132 in ‘struct compat_group_source_req’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:542", # net/compat.c:526:1: warning: alignment 4 of ‘struct compat_group_source_req’ is less than 8 [-Wpacked-not-aligned]
    "compat.c:546", # net/compat.c:530:35: warning: ‘gf_group’ offset 4 in ‘struct compat_group_filter’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:552", # net/compat.c:536:1: warning: alignment 4 of ‘struct compat_group_filter’ is less than 8 [-Wpacked-not-aligned]
    "amba-pl011.c:190", # drivers/tty/serial/amba-pl011.c:190:27: warning: ‘vendor_zte’ defined but not used [-Wunused-variable]
    "exec.c:1198", # warning: argument to ‘sizeof’ in ‘strncpy’ call is the same expression as the source; did you mean to use the size of the destination? [-Wsizeof-pointer-memaccess]
    "printk.c:137", # warning: ‘strncpy’ output truncated before terminating nul copying 2 bytes from a string of the same length [-Wstringop-truncation]
    "printk.c:140", # warning: ‘strncpy’ output truncated before terminating nul copying 2 bytes from a string of the same length [-Wstringop-truncation]
    "task_mmu.c:154", # warning: ‘get_user_pages8’ is deprecated [-Wdeprecated-declarations]
    "dsi_host.c:329", # warning: ‘regulator_can_change_voltage’ is deprecated [-Wdeprecated-declarations]
    "dsi_phy.c:181", # warning: ‘regulator_can_change_voltage’ is deprecated [-Wdeprecated-declarations]
    "core.c:2538", # warning: ‘regulator_can_change_voltage’ is deprecated [-Wdeprecated-declarations]
    "core.c:2522", # warning: ‘regulator_can_change_voltage’ is deprecated [-Wdeprecated-declarations]
    "export.h:63", # warning: ‘regulator_can_change_voltage’ is deprecated [-Wdeprecated-declarations]
 ])

# Capture the name of the object file, can find it.
ofile = None

warning_re = re.compile(r'''(.*/|)([^/]+\.[a-z]+:\d+):(\d+:)? warning:''')
def interpret_warning(line):
    """Decode the message from gcc.  The messages we care about have a filename, and a warning"""
    line = line.rstrip('\n')
    m = warning_re.match(line)
    if m and m.group(2) not in allowed_warnings:
        print "error, forbidden warning:", m.group(2)

        # If there is a warning, remove any object if it exists.
        if ofile:
            try:
                os.remove(ofile)
            except OSError:
                pass
        sys.exit(1)

def run_gcc():
    args = sys.argv[1:]
    # Look for -o
    try:
        i = args.index('-o')
        global ofile
        ofile = args[i+1]
    except (ValueError, IndexError):
        pass

    compiler = sys.argv[0]

    try:
        proc = subprocess.Popen(args, stderr=subprocess.PIPE)
        for line in proc.stderr:
            print line,
            interpret_warning(line)

        result = proc.wait()
    except OSError as e:
        result = e.errno
        if result == errno.ENOENT:
            print args[0] + ':',e.strerror
            print 'Is your PATH set correctly?'
        else:
            print ' '.join(args), str(e)

    return result

if __name__ == '__main__':
    status = run_gcc()
    sys.exit(status)

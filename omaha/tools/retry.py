#!/usr/bin/python

# [Sparrow]
import os.path
import sign_viasat_client_files
# [/Sparrow]
import subprocess
import sys
import time


times = int(sys.argv[1])
duration = float(sys.argv[2])
cmd = sys.argv[3:]


for i in range(times):
  if i:
    print 'Retrying %d...' % i
  retcode = subprocess.call(cmd)
  if retcode == 0:
    # [Sparrow]
    # Only a viasat machine should have this essential file.
    if (os.path.isfile(r'C:\Git\sbb\scripts\slave\omaha_client\restoreEnv.bat') and 
        str(sys.argv[3]).endswith('signtool.exe') and 
        str(sys.argv[-1]).split('.')[-1] in ['exe', 'msi']):
      sign_viasat_client_files.SignAllExeFiles([sys.argv[-1]])
    # [/Sparrow]
    sys.exit(0)
  time.sleep(duration)
sys.exit(retcode)

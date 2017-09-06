#!/usr/bin/python

# [Sparrow]
import os.path
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
    if (os.path.isfile(r'C:\Git\sbb\scripts\slave\sign_viasat_client_files.py') and 
        str(sys.argv[3]).endswith('signtool.exe') and 
        str(sys.argv[-1]).split('.')[-1] in ['exe', 'msi']):
      ret_code = subprocess.call([r'C:\Python27\python.exe', 
                                 r'C:\Git\sbb\scripts\slave\sign_viasat_client_files.py', 
                                 '--files', sys.argv[-1]])
      sys.exit(ret_code)
    # [/Sparrow]
    sys.exit(0)
  time.sleep(duration)
sys.exit(retcode)

# [Sparrow]

import os
import os.path
import socket
import subprocess

def SignAllExeFiles(payload_contents):
  """Sends all executable files in the payload_contents to the Windows
  signing server for signing. This must be done a bit roundabout since 
  only one machine can send files to the Windows signing server.
  We send a list of commands to a file that runs them in parallel. These
  commands call a file on the master that moves the executables to and
  from the Windows signing server.
  """

  
  # Due to the new architecture, we only receive one file at a time now,
  # so the parallel setup is superflorous, but the code works, and I don't
  # have time to refactor.
  # This is an example of the google signing command that just ran before this code.
  # sign /f "C:\Crystalnix\omaha\omaha/data/OmahaTestCert.pfx" /p "test" /t "http://timestamp.verisign.com/scripts/timestamp.dll" "scons-out\opt-win\obj\google_update\ViaSatUpdate_signed.exe"

  whitelist = {"ViaSatUpdate_signed.exe",
               "ViaSatCrashHandler.exe",
               "ViaSatUpdateHelper.msi",
               "ViaSatUpdateBroker.exe",
               "ViaSatUpdateOnDemand.exe",
               "ViaSatUpdateComRegisterShell64.exe",
               "ViaSatUpdateWebPlugin.exe",
               "ViaSatCrashHandler64.exe"
              }
  
  ### START Helper Functions ###
  # Helper functions declared here to contain scope.
  # I want to avoid name conflicts with Crystalnix and Google code as much as possible without a lot of overhead.
  
  def LoadEnv(scripts_dir):
    # Load environment variables.
    
    env = {}
    env_file_path = os.path.join(scripts_dir, r'omaha_client\restoreEnv.bat')
    
    env_file = open(env_file_path, 'r')
    try:
      for line in env_file:
        if line.startswith('set '):
          label, value = line[4:].strip().split('=', 1)
          env[label] = value
    except:
      print "Failed to open restoreEnv.bat"
      raise
    finally:
      env_file.close()
    
    return env
  
  def GetFiles(payload_contents):
    # Collect EXE and MSI files.

    # Files to sign should be in C:\Crystalnix\omaha\omaha\scons-out\opt-win\staging
    exe_files = ['/cygdrive/c/Crystalnix/omaha/omaha/' + '/'.join(item.split('\\')) 
                 for item in payload_contents 
                 if os.path.isfile(item) and (item.lower().endswith('.exe') or item.lower().endswith('.msi'))
                ]
    return exe_files
  
  def GetCygwinPath():
    # These are two standard Cygwin installation paths.
    # If a dev uses a different installation, this code could get way more complicated
    # because of the path rewriting in GetFiles.
    # We're using double slashes because these strings need to get interpolated into the parallel command.
    if os.path.isfile(r'C:\cygwin64\bin\bash.exe'):
      return r'C:\\cygwin64\\bin\\bash'
    elif os.path.isfile(r'C:\cygwin\bin\bash.exe'):
      return r'C:\\cygwin\\bin\\bash'
    else:
      raise Exception(r"Could not find Cygwin installation. (C:\cygwin64\bin\bash and C:\cygwin\bin\bash not found.)")
  
  def GetSlaveIp():
    # Collect IP address and username of this machine.

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.connect(('google.com', 0))
    return sock.getsockname()[0]
  
  def BuildParallelCommand(env, exe_files):
    # Format and string together a string to send to the parallel_command_tool.py.

    cygwin = GetCygwinPath()
    slave_ip = GetSlaveIp()

    command_list = []
    for exe_file in exe_files:
      # This is a list of strings that will get parsed into JSON and sent to the command line. Since both the comamnd line
      # and JSON use double quotes, the string needs to escape the inner pair with slashes. Because we don't have a JSON 
      # module, we have to do this intricate procedure. It may be possible to optimize this, but this is easier to follow.

      # (r'C:\cygwin64\bin\bash --login -c "ssh <proxy-un>@<proxy_host_ip> \"python 
      #   /home/viasat/Git/sparrow_buildbot/scripts/slave/windows_exe_signer.py --host <host_ip> --username <username> 
      #   --file /cygdrive/c/Crystalnix/omaha/scons-out/opt-win/staging/ViaSatUpdate.exe\""')
      command_list.append([
                           cygwin, '--login', '-c',
                           ('ssh -i /home/viasat/.ssh/obs-rsa viasat@%s ' % (env['TESTING_MASTER_HOST'],)) +
                           ('\\"python /home/viasat/Git/sparrow_buildbot/scripts/slave/windows_exe_signer.py ') +
                           ('--host %s --username %s --file %s\\"' % (
                                                                      slave_ip, env['USERNAME'],
                                                                      # Replace one slash with two slashes, so we
                                                                      # can keep one slash when converting to JSON.
                                                                      exe_file.replace("\\", "\\\\")
                                                                     )
                           )
                          ]
                         )
    return command_list
  
  def SignFiles(env, python, scripts_dir, exe_files):
    # Build a string and send it to the parallel_command_tool.py.

    command_list = BuildParallelCommand(env, exe_files)
    
    try:
      # Again, we must adapt to the oppresive regime of Python 2.4 with str and replace
      # because Python 2.4 doesn't have a json module.
      # Hey, guess what? subprocess.check_call doesn't exist either! We're reduced to call.
      ret = subprocess.call(
                            [
                             python, os.path.join(scripts_dir, "parallel_command_tool.py"), 
                             "--commands",
                             # This is how you convert a python object to JSON without JSON.
                             # Take the Python string representation of the object and replace 
                             # the single quotes with double quotes and remove the escape slashes
                             # that Python adds when generating the string.
                             str(command_list).replace("'", '"').replace("\\\\", "\\")
                            ]
                           )
      if ret != 0:
        errStr = "Signing executable files failed for unknown reason.\n%s" % (command_list,)
        raise Exception(errStr)
    except:
      print "Signing executable files failed.\n%s" % (command_list,)
      raise
  
  ### END Helper Functions ###
  
  # This is a standard Windows python installation path.
  python27 = r"C:\Python27\python.exe"
  # This is a de-facto office standard. Devs are expected to conform.
  scripts_dir = r"C:\Git\sbb\scripts\slave"

  approved_files = []
  for file_path in payload_contents:
    if file_path.rsplit('\\', 1)[-1] in whitelist:
      approved_files.append(file_path)

  env = LoadEnv(scripts_dir)
  
  exe_files = GetFiles(approved_files)

  SignFiles(env, python27, scripts_dir, exe_files)

#[/Sparrow]

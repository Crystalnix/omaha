# [Sparrow]

import os
import os.path
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

  
  ### START Helper Functions ###
  # Helper functions declared here to contain scope.
  # I want to avoid name conflicts with Crystalnix and Google code as much as possible without a lot of overhead.
  
  def LoadEnv():
  # Load environment variables.
  with open(r'C:\Git\sbb\scripts\slave\omaha_client\restoreEnv.bat', 'r') as env_file:
    
  
  def GetFiles(payload_contents):
    # Collect EXE and MSI files.

    # Files to sign should be in C:\Crystalnix\omaha\omaha\scons-out\opt-win\staging
    exe_files = ['/cygdrive/c/Crystalnix/omaha/omaha/' + '/'.join(item.split('\\')[1:]) 
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
  
  def GetSlaveInfo(env, python, scripts_dir):
    # Collect IP address and username of this machine.

    # Python 2.4 doesn't support check_output. We're reduced to Popen.
    info_json_cmd = subprocess.Popen([python, os.path.join(scripts_dir, "get_slave_info.py")], 
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    info_json, stderr = info_json_cmd.communicate()

    if info_json_cmd.returncode:
      print "Failed to read network properties from slave."
      raise Exception(stderr)
    # Python 2.4, no json module :(
    return eval(info_json.split('=')[-1].strip())
  
  def BuildParallelCommand(env, python, scripts_dir, exe_files):
    # Format and string together a string to send to the parallel_command_tool.py.

    cygwin = GetCygwinPath()
    info = GetSlaveInfo(env, python, scripts_dir)

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
                           ('ssh -i /home/viasat/.ssh/obs-rsa viasat@%s ' % (env('TESTING_MASTER_HOST'),)) +
                           ('\\"python /home/viasat/Git/sparrow_buildbot/scripts/slave/windows_exe_signer.py ') +
                           ('--host %s --username %s --file %s\\"' % (
                                                                      info['slave_ip'], info['slave_username'],
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

    command_list = BuildParallelCommand(env, python, scripts_dir, exe_files)
    
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

  env = LoadEnv(scripts_dir)
  
  exe_files = GetFiles(payload_contents)

  SignFiles(env, python27, scripts_dir, exe_files)

#[/Sparrow]

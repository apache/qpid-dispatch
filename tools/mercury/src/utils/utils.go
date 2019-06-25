/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */



package utils

import ( "fmt"
         "net"
         "os"
         "os/exec"
         "os/user"
         "strconv"
         "strings"
         "runtime" 
         "time" 
       )


var fp = fmt.Fprintf
var mercury = '\u263F'




func Check ( err error ) {
  if err != nil {
    panic ( err )
  }
}




func Get_homedir ( ) ( string ) {
  usr, _ := user.Current()
  return usr.HomeDir
}





func M_error ( format string, args ...interface{}) {
  new_format := fmt.Sprintf ( "    %c error: %s", mercury, format + "\n" )
  fp ( os.Stdout, "\n------------------------------------------------\n" )
  fp ( os.Stdout, new_format, args ... )
  fp ( os.Stdout,   "------------------------------------------------\n\n" )
}





func M_info ( verbose bool, format string, args ...interface{}) {
  if ! verbose {
    return
  }
  new_format := fmt.Sprintf ( "    %c info: %s\n", mercury, format )
  fp ( os.Stdout, new_format, args ... )
}





func Available_port () ( port string, err error ) {

  server, err := net.Listen ( "tcp", ":0" )
  if err != nil {
    return "", err
  }
  defer server.Close()

  hostString := server.Addr().String()

  _, portString, err := net.SplitHostPort(hostString)
  if err != nil {
    return "", err
  }

  return portString, nil
}





func Path_exists ( path string ) ( bool ) {

  _, err := os.Stat ( path )

  if nil != err && os.IsNotExist ( err ) { 
    return false
  }

  return true
}





func Print_Callstack ( ) { 
  var file        string
  var line_number int
  var ok          bool

  caller_number := 1

  for {
    _, file, line_number, ok = runtime.Caller ( caller_number ) 
    if ! ok {
      break
    }
    fp ( os.Stdout, "    Called from: file %s line %d\n", file, line_number )
    caller_number += 1
  }
}





func Find_or_create_dir ( path string ) {
  _, err := os.Stat ( path )
  if os.IsNotExist ( err ) {
    err = os.MkdirAll ( path, os.ModePerm )
    if err != nil {
        fp ( os.Stderr, "utils.Find_or_create_dir : error creating dir |%s| : %v\n", path, err )

        Print_Callstack ( )
        os.Exit ( 1 )
    }
  }
}





func End_test_and_exit ( result_path string, test_error string ) {
  f, err := os.Create ( result_path + "/result" )
  if err != nil {
    fp ( os.Stderr, "Can't write results file!\n" )
    os.Exit ( 1 )
  }
  defer f.Close ( )

  if test_error == "" {
    fp ( f, "success\n" )
  } else {
    fp ( f, "failure : %s\n", test_error )
  }

  os.Exit ( 0 )
}





func Make_paths ( mercury_root, test_id, test_name string ) ( router_path, result_path, config_path, log_path string ) {
  dispatch_install_root := os.Getenv ( "DISPATCH_INSTALL_ROOT" )
  router_path            = dispatch_install_root + "/sbin/qdrouterd"
  result_path            = mercury_root + "/results/" + test_name + "/" + test_id
  config_path            = result_path + "/config"
  log_path               = result_path + "/log"

  return router_path, result_path, config_path, log_path
}





func Memory_usage ( pid int ) ( rss int ) {
  proc_file_name := "/proc/" + strconv.Itoa(pid) + "/statm"
  proc_file, err := os.Open ( proc_file_name )
  if err != nil {
    fp ( os.Stderr, "util.Memory_usage error: can't open |%s|\n", proc_file_name )
    return -1
  }
  defer proc_file.Close ( )

  var vm_size int
  fmt.Fscanf ( proc_file, "%d%d", & vm_size, & rss )
  return rss
}





func Cpu_usage ( target_pid int ) ( cpu_usage int, err error ) {

  // Let top iterate twice for greater accuracy.
  command   := "top" 
  args      := " -b -n 2 -p " + strconv.Itoa ( target_pid )
  args_list := strings.Fields ( args )

  out, err := exec.Command ( command, args_list... ).Output()
  if err != nil {
    return 0, err
  }

  lines := strings.Split ( string(out), "\n" )

  last_line := lines [ len(lines) - 2 ]
  fields := strings.Fields ( last_line )
  cpu_field := fields [ 8 ]
  temp, err := strconv.ParseFloat ( cpu_field, 32 )
  if err != nil {
    return 0, err
  }
  cpu_usage = int ( 100 * temp )

  return cpu_usage, nil
}





func Timestamp ( ) ( string ) {
  now := time.Now()
  ts1 := now.Format ( "2006-01-02 15:04:05.000000" )
  nsec := now.UnixNano()
  fsec := float64(nsec) / 1000000000.0
  return ts1 + fmt.Sprintf ( " %.6f", fsec )
}





// Use cpupower command to discover allowable frequencies.
func Get_CPU_freqs ( ) ( freqs [] string ) {
  command   := "sudo"
  args      := "cpupower frequency-info"
  args_list := strings.Fields ( args )

  out, _ := exec.Command ( command, args_list... ).Output()

  lines := strings.Split ( string(out), "\n" )
  for _, line := range lines {
    index := strings.Index ( line, "available frequency steps" )
    if index != -1 {
      fields := strings.Fields ( line )
      for _, field := range fields {
        _, err := strconv.ParseFloat ( field, 32 )
        if err == nil {
          freqs = append ( freqs, field ) // I want the string, though. Not the float.
        }
      }
      break
    }
  }

  return freqs
}





func Set_Top_Freq ( freqs [] string ) ( err error ) {
  command := "sudo"
  args    := "cpupower frequency-set --freq " + freqs [ 0 ] + "GHz"

  fp ( os.Stdout, "Set_Top_Freq: command: |%s %s|\n", command, args )
  
  return nil
}






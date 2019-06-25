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


/*
  Package router implements a dispatch router.
  The normal order for operations on a router is:
    * create
    * connect to other routers
    * init
    * run
    * halt
  It is during the initialization step that the 
  configuration file is written that will be read at 
  router startup. So any connecting that you want to do
  should be done before you call Init.
  Every router is created with a normal-mode listener, so 
  you can always attach a client to it, or send it 
  management commands.
*/
package router

import ( "errors"
         "fmt"
         "os"
         "os/exec"
         "syscall"
         "strings"
         "time"
         "utils"
       )


var fp          = fmt.Fprintf
var module_name = "router"
var ume         = utils.M_error
var umi         = utils.M_info






type router_state int

const (
  none         router_state = iota
  initialized
  running                 
  halted
)





func state_to_string ( state router_state ) ( string ) {
  switch state {
    case initialized :
        return "initialized"
    case running :
        return "running"
    case halted :
      return "halted"
    default :
      return "none"
  }
}





/*
  The Router struct represents a Dispatch Router,
  maintains state about it, remembers all the paths 
  it needs to know, and contains the exec command 
  that is used to manipulate the running process.

  The router runs as a separate process from the main 
  program.
*/
type Router struct {
  name                           string
  version                        string
  router_type                    string
  worker_threads                 int
  executable_path                string
  config_path                    string
  log_path                       string
  Log_file_path                  string
  config_file_path               string
  ld_library_path                string
  pythonpath                     string
  include_path                   string
  console_path                   string
  client_port                    string
  console_port                   string
  router_port                    string
  edge_port                      string
  verbose                        bool

  pid                            int
  state                          router_state            
  cmd                          * exec.Cmd
  i_connect_to_ports            [] string
  I_connect_to_names            [] string

  // Names of routers that connect to me.
  Connect_to_me_interior        [] string
  connect_to_me_edge            [] string
}



func ( r * Router ) Is_connected_to ( other_router_name string ) ( bool ) {
  for _, i_connect_to := range r.I_connect_to_names {
    if i_connect_to == other_router_name {
      return true
    }
  }

  for _, connect_to_me := range r.Connect_to_me_interior {
    if connect_to_me == other_router_name {
      return true
    }
  }

  return false
}



/*
  Create a new router.
  The caller supplies all the paths and ports.
  Every router will be given a listener for clients
  to connect to. Even if a particular test does not
  need clients to connect to this router, the support
  libraries will sometimes need to talk to this port.

  Interior routers will also be given a listener for 
  other interior routers, and a separate listener for
  edge routers.
*/
func New_Router ( name                        string, 
                  version                     string,
                  router_type                 string,
                  worker_threads              int,
                  executable_path             string,
                  config_path                 string,
                  log_path                    string,
                  include_path                string,
                  console_path                string,
                  ld_library_path             string,
                  pythonpath                  string,
                  client_port                 string,
                  console_port                string,
                  router_port                 string,
                  edge_port                   string,
                  verbose                     bool ) * Router {
  var r * Router

  r = & Router { name            : name, 
                 version         : version,
                 router_type     : router_type,
                 worker_threads  : worker_threads,
                 executable_path : executable_path,
                 config_path     : config_path,
                 log_path        : log_path,
                 include_path    : include_path,
                 console_path    : console_path,
                 ld_library_path : ld_library_path,
                 pythonpath      : pythonpath,
                 client_port     : client_port,
                 console_port    : console_port,
                 router_port     : router_port,
                 edge_port       : edge_port,
                 verbose         : verbose }

  return r
}





func ( r * Router ) Verbose ( val bool ) {
  r.verbose = val
}





func ( r * Router ) Edges ( ) ( [] string ) {
  return r.connect_to_me_edge
}





func ( r * Router ) Connected_to_you ( router_name string, edge bool ) {
  if edge {
    r.connect_to_me_edge = append ( r.connect_to_me_edge, router_name )
  } else {
    r.Connect_to_me_interior = append ( r.Connect_to_me_interior, router_name )
  }
}





func ( r * Router ) Print_console_port () {
  fp ( os.Stdout, "  router %s console port %s\n", r.name, r.console_port )
}





func ( r * Router ) Print () {
  fp ( os.Stdout, "router %s -------------\n", r.name )
  fp ( os.Stdout, "  PID: %d\n", r.pid )
  fp ( os.Stdout, "  state:  %s\n", state_to_string ( r.state ) )
  fp ( os.Stdout, "  client  port: %s\n", r.client_port )
  fp ( os.Stdout, "  router  port: %s\n", r.router_port )
  fp ( os.Stdout, "  edge    port: %s\n", r.edge_port )
  fp ( os.Stdout, "  console port: %s\n", r.console_port )
  fp ( os.Stdout, "\n" )

  if 0 < len(r.I_connect_to_names) {
    fp ( os.Stdout, "  Routers that I connect to:\n" )
    for i, name := range r.I_connect_to_names {
      fp ( os.Stdout, "    %s %s\n", name, r.i_connect_to_ports [ i ] )
    }
  }

  if 0 < len(r.Connect_to_me_interior) {
    fp ( os.Stdout, "  Interior routers that connect to me:\n" )
    for _, name := range r.Connect_to_me_interior {
      fp ( os.Stdout, "    %s\n", name )
    }
  }

  if 0 < len(r.connect_to_me_edge) {
    fp ( os.Stdout, "  Edge routers that connect to me:\n" )
    for _, name := range r.connect_to_me_edge {
      fp ( os.Stdout, "    %s\n", name )
    }
  }

  fp ( os.Stdout, "\n" )
}





// Tell the router a port number (represented as a string)
// that it should attach to.
func ( r * Router ) Connect_to ( name string, port string ) {
  r.i_connect_to_ports = append ( r.i_connect_to_ports, port )
  r.I_connect_to_names = append ( r.I_connect_to_names, name )
}





// Get the router's name.
func ( r * Router ) Name ( ) string  {
  return r.name
}





func ( r * Router ) Is_interior () (bool) {
  return r.router_type == "interior"
}





/*
  Get the router's type, i.e. interior or edge.
*/
func ( r * Router ) Type ( ) string  {
  return r.router_type
}





/*
  Get the router's client port number (as a string).
*/
func ( r * Router ) Client_port ( ) string {
  return r.client_port
}





/*
  Get the router's router port number (as a string).
  Or nil if this is an edge router.
*/
func ( r * Router ) Router_port ( ) string {
  return r.router_port
}





/*
  Get the router's edge port number (as a string).
  Or nil if this is an edge router.
*/
func ( r * Router ) Edge_port ( ) string {
  return r.edge_port
}





/*
  Initialization of a router does whatever is needed 
  to get ready to launch the router, i.e. write the
  configuration file.
*/
func ( r * Router ) Init ( ) error {
  if r.state >= initialized {
    return nil
  }

  r.config_file_path = r.config_path + "/" + r.name + ".conf"
  r.state = initialized
  return r.write_config_file ( )
}





func ( r * Router ) write_config_file ( ) error {
  f, err := os.Create ( r.config_file_path )
  if err != nil {
    return err
  }
  defer f.Close ( )

  fp ( f, "router {\n" )
  fp ( f, "  workerThreads : %d\n", r.worker_threads )
  fp ( f, "  mode          : %s\n", r.router_type )
  fp ( f, "  id            : %s\n", r.name )
  fp ( f, "}\n" )

  fp ( f, "address {\n" );
  fp ( f, "  prefix       : closest\n" );
  fp ( f, "  distribution : closest\n" );
  fp ( f, "}\n" )

  /*
  fp ( f, "address {\n" );
  fp ( f, "  prefix       : speedy\n" );
  fp ( f, "  distribution : closest\n" );
  fp ( f, "  priority     : 8\n" );
  fp ( f, "}\n" )
  */

  fp ( f, "address {\n" );
  fp ( f, "  prefix       : balanced\n" );
  fp ( f, "  distribution : balanced\n" );
  fp ( f, "}\n" )

  fp ( f, "address {\n" );
  fp ( f, "  prefix       : multicast\n" );
  fp ( f, "  distribution : multicast\n" );
  fp ( f, "}\n" )

  r.Log_file_path = r.log_path + "/" + r.name + ".log"

  fp ( f, "log {\n" )
  fp ( f, "  outputFile    : %s\n", r.Log_file_path )
  // Use this if you want no output.
  //fp ( f, "  enable        : none\n" )
  fp ( f, "  includeSource : true\n" )
  fp ( f, "  module        : DEFAULT\n" )
  fp ( f, "}\n" )

  /*
  link_capacity := 250
  if r.name == "E" {
    link_capacity = 10000
    fp ( os.Stdout, "ingress router  client linkCap == %d\n", link_capacity )
  }
  */

  // The Client Listener -----------------
  fp ( f, "listener {\n" )
  fp ( f, "  role               : normal\n")
  fp ( f, "  host               : 0.0.0.0\n")
  fp ( f, "  port               : %s\n", r.client_port )
  fp ( f, "  stripAnnotations   : no\n")
  fp ( f, "  idleTimeoutSeconds : 120\n")
  fp ( f, "  saslMechanisms     : ANONYMOUS\n")
  fp ( f, "  authenticatePeer   : no\n")
  //fp ( f, "  linkCapacity       : %d\n", link_capacity )
  fp ( f, "}\n")

  // The Console Listener -----------------
  fp ( f, "listener {\n" )
  fp ( f, "  role               : normal\n")
  fp ( f, "  host               : 0.0.0.0\n")
  fp ( f, "  port               : %s\n", r.console_port )
  fp ( f, "  stripAnnotations   : no\n")
  fp ( f, "  idleTimeoutSeconds : 120\n")
  fp ( f, "  saslMechanisms     : ANONYMOUS\n")
  fp ( f, "  authenticatePeer   : no\n")
  fp ( f, "  http               : true\n")
  //fp ( f, "  httpRoot           : %s\n", r.console_path)
  fp ( f, "}\n")

  // The Router Listener -----------------
  if r.router_type != "edge" {
    fp ( f, "listener {\n" )
    fp ( f, "  role               : inter-router\n")
    fp ( f, "  host               : 0.0.0.0\n")
    fp ( f, "  port               : %s\n", r.router_port )
    fp ( f, "  stripAnnotations   : no\n")
    fp ( f, "  idleTimeoutSeconds : 120\n")
    fp ( f, "  saslMechanisms     : ANONYMOUS\n")
    fp ( f, "  authenticatePeer   : no\n")
    fp ( f, "}\n")
  }

  /*
  */
  // The Edge Listener -----------------
  if r.router_type != "edge" {
    // Edge routers do not get an edge listener.
    fp ( f, "listener {\n" )
    fp ( f, "  role               : edge\n")
    fp ( f, "  host               : 0.0.0.0\n")
    fp ( f, "  port               : %s\n", r.edge_port )
    fp ( f, "  stripAnnotations   : no\n")
    fp ( f, "  idleTimeoutSeconds : 120\n")
    fp ( f, "  saslMechanisms     : ANONYMOUS\n")
    fp ( f, "  authenticatePeer   : no\n")
    fp ( f, "}\n")
  }

  // The Connectors --------------------
  for _, port := range r.i_connect_to_ports {
    // fp ( os.Stderr, "router %s connect to %s\n", r.name, port )
    fp ( f, "connector {\n" )
    //fp ( f, "  verifyHostname     : no\n")
    fp ( f, "  name               : %s_connector_to_%s\n", r.name, port)
    fp ( f, "  idleTimeoutSeconds : 120\n")
    fp ( f, "  saslMechanisms     : ANONYMOUS\n")
    fp ( f, "  host               : 127.0.0.1\n")
    fp ( f, "  port               : %s\n", port)
    if r.router_type == "edge" {
      fp ( f, "  role               : edge\n")
    } else {
      fp ( f, "  role               : inter-router\n")
    }
    fp ( f, "}\n")
  }

  umi ( r.verbose, "router |%s| config file written to |%s|", r.name, r.config_file_path )

  return nil
}





/*
  Call this only after calling Init() on the router.
  This fn sets up the router's environment variables, 
  and runs the router as a separate process.
*/
func ( r * Router ) Run ( ) error {
  if r.state == running {
    ume ( "Attempt to re-run running router |%s|.", r.name )
    return nil
  }

  // Set up the environment for the router process.
  os.Setenv ( "LD_LIBRARY_PATH", r.ld_library_path )
  os.Setenv ( "PYTHONPATH"     , r.pythonpath )

  router_args     := " --config " + r.config_file_path + " -I " + r.include_path
  args            := router_args

  args_list := strings.Fields ( args )

  // Start the router process and get its pid for the result directory name.
  // After the Start() call, the router process is running detached.
  r.cmd = exec.Command ( r.executable_path,  args_list... )
  if r.cmd == nil {
    fp ( os.Stdout, "   router.Run error: can't execute |%s|\n", r.executable_path )
    return errors.New ( "Can't execute router executable." )
  }
  r.cmd.Start ( )
  r.state = running

  if r.cmd.Process == nil {
    fp ( os.Stdout, "   router.Run error: can't execute |%s|\n", r.executable_path )
    return errors.New ( "Can't execute router executable." )
  }

  r.pid = r.cmd.Process.Pid

  umi ( r.verbose, "Router |%s| has started with pid %d .", r.name, r.pid )

  // Write the environment variables to the config directory.
  // This helps the user to reproduce this test, if desired.
  env_file_name := r.config_path + "/" + r.name + "_environment_variables"
  env_file, err := os.Create ( env_file_name )
  utils.Check ( err )
  defer env_file.Close ( )
  env_string := "export LD_LIBRARY_PATH=" + r.ld_library_path + "\n"
  env_file.WriteString ( env_string )
  env_string  = "export PYTHONPATH=" + r.pythonpath + "\n"
  env_file.WriteString ( env_string )

  // Write the command line to the results directory.
  // This helps the user to reproduce this test, if desired.
  command_file_name := r.config_path + "/" + r.name + "_command_line"
  command_file, err := os.Create ( command_file_name )
  utils.Check ( err )
  defer command_file.Close ( )
  command_string := r.executable_path + " " + args
  command_file.WriteString ( command_string + "\n" )

  return nil
}





func ( r * Router ) Is_not_halted ( ) ( bool ) {
  return "halted" != r.State()
}





func ( r * Router ) State ( ) ( string ) {

  switch r.state {

    case none: 
      return "none"

    case initialized:
      return "initialized"

    case running:
      return "running"

    case halted:
      return "halted"
  }

  return "error"
}





/*
  Halt the router.
  If it has already halted on its own, that is returned
  as an error. If the process returned an error code, 
  return that to the caller -- but early termination is
  considered an error even if the process did not return 
  an error code.
*/
func ( r * Router ) Halt ( ) error {
  if r.verbose {
    fp ( os.Stdout, "halting router |%s|\n", r.name )
  }

  if r.state == halted {
    ume ( "Attempt to re-halt router |%s|.", r.name )
    return nil
  }

  // Set up a channel that will return a 
  // message immediately if the process has
  // already terminated. Then set up a half-second 
  // timer. If the timer expires before the Wait 
  // returns a 'done' message, we judge that the 
  // process was still running when we came along
  // and killed it. Which is good.
  done := make ( chan error, 1 )
  go func ( ) {
      done <- r.cmd.Wait ( )
  } ( )

  // In any case, the router is now halted.
  r.state = halted

  select {
    /*
      This is the expected case.
      Our timer times out while the above Wait() is still waiting.
      This means that the process is still running normally when we kill it.
    */
    case <-time.After ( 250 * time.Millisecond ) :

      // Don't just kill it! Give the router a chance to shut down.
      // if err := r.cmd.Process.Kill(); err != nil {
      if err := r.cmd.Process.Signal( syscall.SIGTERM ); err != nil {
        return errors.New ( "failed to kill process: " + err.Error() )
      }

      // This is the good case. It was not already dead when 
      // we came here, and we successfully halted it.
      umi ( r.verbose, "Router |%s| halted.", r.name )
      return nil

    case err := <-done:
      if err != nil {
        return errors.New ( "process terminated early with error: " + err.Error() )
      }

      // Even though there was no error reported -- the process 
      // mevertheless stopped early, which is an error in the 
      // context of this test.
      return errors.New ( "process self-terminated." )
  }

  return nil
}





func ( r * Router ) Pid ( ) ( int )  {
  return int ( r.cmd.Process.Pid )
}






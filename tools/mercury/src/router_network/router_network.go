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


package router_network

import ( "errors"
         "fmt"
         "io/ioutil"
         "os"
         "strings"
         "math/rand"
         "sync"
         "time"

         "client"
         "router"
         "utils"
       )


var fp          = fmt.Fprintf
var module_name = "router_network"
var ume         = utils.M_error
var umi         = utils.M_info





type Version struct {
  Name            string

  dispatch_root   string
  proton_root     string

  Router_path     string
  Pythonpath      string
  Ld_library_path string
  Console_path    string
  Include_path    string
}





func check_path ( name string, path string, must_exist bool ) {
  if ! utils.Path_exists ( path ) {
    if must_exist {
      ume ( "rn.check_path: Path |%s| does not exist at |%s|", name, path )
      os.Exit ( 1 )
    } else {
      umi ( true, "rn.check_path: Path |%s| does not exist at |%s|", name, path )
    }
  }
}





/*===================================================================

  The purpose of the two Version constructors is to allow you to 
  construct a Version the easy way -- with just two install paths --
  or the hard way, by supplying all paths yourself. 

  This is because some environments are not set up as my code 
  expects them to be, and you may need the flexibility of the 
  explicit paths method.

  You call these indirectly, by calling one of 
    Add_version_with_roots(), or
    Add_version_with_paths().
    
-===================================================================*/

func new_version_with_roots ( name          string,
                              dispatch_root string,
                              proton_root   string,
                              verbose       bool ) * Version {

  // First make sure that what the caller gave us is real.
  check_path ( "dispatch root", dispatch_root, true )
  check_path ( "proton root",   proton_root,   true )

  v := & Version { Name          : name,
                   dispatch_root : dispatch_root,
                   proton_root   : proton_root } 
  v.Router_path = dispatch_root + "/sbin/qdrouterd"
  check_path ( "router path", v.Router_path, true )

  // Calculate LD_LIBRARY_PATH for this version.
  DISPATCH_LIBRARY_PATH := v.dispatch_root + "/lib"
  PROTON_LIBRARY_PATH   := v.proton_root   + "/lib64"
  v.Ld_library_path      = DISPATCH_LIBRARY_PATH +":"+ PROTON_LIBRARY_PATH
  check_path ( "dispatch library path", DISPATCH_LIBRARY_PATH, true )
  check_path (   "proton library path",   PROTON_LIBRARY_PATH, true )


  // Calculate PYTHONPATH for this version.
  DISPATCH_PYTHONPATH   := DISPATCH_LIBRARY_PATH + "/qpid-dispatch/python"
  DISPATCH_PYTHONPATH2  := DISPATCH_LIBRARY_PATH + "/python2.7/site-packages"
  PROTON_PYTHONPATH     := PROTON_LIBRARY_PATH   + "/proton/bindings/python"
  check_path ( "dispatch python path",  DISPATCH_PYTHONPATH,  true )
  check_path ( "dispatch pythonpath 2", DISPATCH_PYTHONPATH2, true )
  check_path ( "proton python path",    PROTON_PYTHONPATH,    true )

  v.Pythonpath          =  DISPATCH_PYTHONPATH +":"+ DISPATCH_PYTHONPATH2 +":"+ PROTON_LIBRARY_PATH +":"+ PROTON_PYTHONPATH
  v.Console_path        =  v.dispatch_root + "/share/qpid-dispatch/console"
  v.Include_path        =  v.dispatch_root + "/lib/qpid-dispatch/python"
  check_path ( "include path", v.Include_path, true )
  check_path ( "console path", v.Console_path, false ) // The console is an optional install.

  umi ( verbose, "router path     |%s|", v.Router_path )
  umi ( verbose, "ld library path |%s|", v.Ld_library_path )
  umi ( verbose, "python path     |%s|", v.Pythonpath )
  umi ( verbose, "include path    |%s|", v.Include_path )
  umi ( verbose, "console path    |%s|", v.Console_path )

  return v
}





func new_version_with_paths ( name            string,
                              router_path     string,
                              pythonpath      string,
                              ld_library_path string,
                              include_path    string ) * Version {

  v := & Version { Name            : name,
                   Router_path     : router_path,
                   Pythonpath      : pythonpath,
                   Ld_library_path : ld_library_path,
                   Include_path    : include_path }

  check_path ( "Router_path",     router_path,     true )
  //check_path ( "Pythonpath",      pythonpath,      true )
  check_path ( "Include_path",    include_path,    true )
  // check_path ( "Ld_library_path", ld_library_path, true )

  var paths = strings.Split ( pythonpath, ":")
  for _, path := range paths  {
    check_path ( "Pythonpath", path, true )
  }

  paths = strings.Split ( ld_library_path, ":")
  for _, path := range paths  {
    check_path ( "Ld_library_path", path, true )
  }



  // In this constructor, the two 'roots' 
  // are left nil. They will never be used.
  return v
}





type Router_network struct {
  Name                        string
  Running                     bool

  results_path                string
  log_path                    string

  /*
    The Network, rather than the Version has
    the client path, because the client comes
    from the Mercury install, not from the Dispatch
    or Proton installs, which are contained in Version.
  */
  client_path                 string

  Versions               [] * Version
  Default_version           * Version

  verbose                     bool

  routers                [] * router.Router
  clients                [] * client.Client

  ticker_frequency            int
  client_ticker             * time.Ticker
  client_status_files    []   string
  completed_clients           int

  channel                     chan string

  n_senders                   int

  Failsafe                    int
  failsafe_timer            * time.Ticker

  init_only                   bool
}





func ( rn * Router_network ) Kill_and_restart_random_client ( ) {
  n_clients := len ( rn.clients )
  if n_clients <= 0 {
    ume ( "router_network.Kill_and_restart_random_client error: no clients.\n" )
    return
  }

  client_number := rand.Intn ( n_clients )
  fp ( os.Stdout,  "Kill_and_restart_random_client: %d\n", client_number )

  client := rn.clients [ client_number ]
  client.Kill_and_restart ( 15 )
}





func ( rn * Router_network ) First_router_name ( ) ( string ) {
   fp ( os.Stdout, "n_routers: %d\n", len ( rn.routers ) )
  return rn.routers[0].Name()
}





func ( rn * Router_network ) Last_router_name ( ) ( string ) {
  n_routers := len(rn.routers)-1
  return rn.routers [ n_routers ].Name()
}





func ( rn * Router_network ) Init_only ( val bool ) {
  rn.init_only = val
}





func ( rn * Router_network ) Reset ( ) {
  rn.Halt ( )
  rn.Running         = false
  rn.Versions        = nil
  rn.Default_version = nil
  rn.verbose         = false
  rn.routers         = nil
  rn.clients         = nil
  rn.n_senders       = 0
  rn.Failsafe        = 0
  rn.failsafe_timer  = nil
  rn.init_only       = false
}





func ( rn * Router_network ) get_client_by_name ( target_name string ) ( * client.Client ) {
  for _, c := range rn.clients {
    if target_name == c.Name {
      return c
    }
  }
  return nil
}





// Create a new router network.
// Tell it how many worker threads each router should have,
// and provide lots of paths.
func New_router_network ( name         string,
                          mercury_root string,
                          log_path     string,
                          channel      chan string ) * Router_network {

  rn := & Router_network { Name         : name,
                           log_path     : log_path,
                           channel      : channel }

  rn.client_path = mercury_root + "/clients/c_proactor_client" 
  if ! utils.Path_exists ( rn.client_path  ) {
    ume ( "network error; client path |%s| does not exist. It probably needs to be built.", 
          rn.client_path )
    os.Exit ( 1 )
  }

  rn.ticker_frequency = 10

  return rn
}





func ( rn * Router_network ) Set_results_path ( path string ) {
  rn.results_path = path
  utils.Find_or_create_dir ( rn.results_path )
}





func ( rn * Router_network ) Add_version_with_roots ( name          string,
                                                      proton_root   string,
                                                      dispatch_root string ) {

  version := new_version_with_roots ( name, dispatch_root, proton_root, rn.verbose )
  rn.Versions = append ( rn.Versions, version )

  // If this is the first one, make it the default.
  if 1 == len ( rn.Versions ) {
    rn.Default_version = version
  }
}





func ( rn * Router_network ) Add_version_with_paths ( name            string,
                                                      router_path     string,
                                                      ld_library_path string,
                                                      pythonpath      string,
                                                      include_path    string ) {

  version := new_version_with_paths ( name, 
                                      router_path, 
                                      pythonpath, 
                                      ld_library_path,
                                      include_path )
  rn.Versions = append ( rn.Versions, version )

  // If this is the first one, make it the default.
  if 1 == len ( rn.Versions ) {
    rn.Default_version = version
  }
}





func ( rn * Router_network ) Get_n_routers ( ) ( int ) {
  return len ( rn.routers )
}





func ( rn * Router_network ) Get_n_interior_routers ( ) ( int ) {
  count := 0
  for _, r := range rn.routers {
    if r.Type() == "interior" {
      count ++
    }
  }
  return count
}


func ( rn * Router_network ) Get_interior_routers_names ( ) ( [] string ) {
  var interior_router_names [] string
  for _, r := range rn.routers {
    if r.Type() == "interior" { 
      interior_router_names = append ( interior_router_names, r.Name() )
    }
  }

  return interior_router_names
}





func ( rn * Router_network ) Get_router_log_file_paths ( router_names [] string )  ( [] string ) {
  var log_file_names [] string
  for _, r_name := range router_names {
    r := rn.get_router_by_name ( r_name )
    log_file_names = append ( log_file_names, r.Log_file_path )
  }

  return log_file_names
}





func ( rn * Router_network ) Get_version_from_name ( target_name string ) ( * Version ) {
  for _, v := range rn.Versions {
    if v.Name == target_name {
      return v
    }
  }
  return nil
}





func ( rn * Router_network ) Get_router_edges ( router_name string ) ( [] string ) {
  rtr := rn.get_router_by_name ( router_name )
  if rtr == nil {
    fp ( os.Stdout, "    network.Get_router_edges error: can't find router |%s|\n", router_name )
    return nil
  }

  return rtr.Edges ( )
}





func ( rn * Router_network ) Print_console_ports ( ) {
  for _, r := range rn.routers {
    r.Print_console_port ( )
  }
}





func ( rn * Router_network ) add_router ( name         string, 
                                          router_type  string, 
                                          version_name string,
                                          config_path  string,
                                          log_path     string ) {
/*
  Some paths are related to the current session. They get passed
  in here directly as args. The others are related to the version
  of the router code we are using, and they come in as part of the
  version structure.
*/
  var console_port string
  if name == "A" {
    console_port = "5673"
  } else {
    console_port, _ = utils.Available_port ( )
  }

  client_port, _  := utils.Available_port ( )
  router_port, _  := utils.Available_port ( )
  edge_port, _    := utils.Available_port ( )

  version := rn.Get_version_from_name ( version_name )

  // TODO -- pass this down from on high
  worker_threads := 16

  r := router.New_Router ( name,
                           version.Name,
                           router_type,
                           worker_threads,
                           version.Router_path,
                           config_path,
                           log_path,
                           version.Include_path,
                           version.Console_path,
                           version.Ld_library_path,
                           version.Pythonpath,
                           client_port,
                           console_port,
                           router_port,
                           edge_port,
                           rn.verbose )
  rn.routers = append ( rn.routers, r )
}





func ( rn * Router_network ) Verbose ( val bool ) {
  rn.verbose = val
  for _, r := range rn.routers {
    r.Verbose ( val )
  }
}





// Add a new router to the network. You can add all routers before
// calling Init, but it's also OK to add more after the network has 
// started. In that case, you must call Init() and Run() again.
// Routers that have already been initialized and started will not 
// be affected.
func ( rn * Router_network ) Add_router ( name         string, 
                                          version_name string,
                                          config_path  string,
                                          log_path     string ) {

  rn.add_router ( name, 
                  "interior", 
                  version_name, 
                  config_path, 
                  log_path )
}





/*
  Similar to Add_Router(), but adds an edge instead of an interior
  router.
*/
func ( rn * Router_network ) Add_edge ( name         string, 
                                        version_name string,
                                        config_path  string,
                                        log_path     string ) {
  rn.add_router ( name, 
                  "edge", 
                  version_name,
                  config_path, 
                  log_path )
}




func ( rn * Router_network ) Add_receiver ( name               string, 
                                            config_path        string,
                                            n_messages         int, 
                                            max_message_length int, 
                                            router_name        string,
                                            delay              string,
                                            soak               string ) {

  throttle := "0" // Receivers do not get throttled.

  rn.add_client ( name, 
                  config_path,
                  rn.results_path,
                  false, 
                  n_messages, 
                  max_message_length, 
                  router_name, 
                  throttle,
                  delay,
                  soak )
}





func ( rn * Router_network ) Add_sender ( name               string, 
                                          config_path        string,
                                          n_messages         int, 
                                          max_message_length int, 
                                          router_name        string, 
                                          throttle           string,
                                          delay              string,
                                          soak               string ) {
  rn.add_client ( name, 
                  config_path,
                  rn.results_path,
                  true, 
                  n_messages, 
                  max_message_length, 
                  router_name, 
                  throttle,
                  delay,
                  soak )
}





func ( rn * Router_network ) add_client ( name               string, 
                                          config_path        string,
                                          results_path       string,
                                          sender             bool, 
                                          n_messages         int, 
                                          max_message_length int, 
                                          router_name        string, 
                                          throttle           string,
                                          delay              string,
                                          soak               string ) {


  var operation string
  if sender {
    operation = "send"
    rn.n_senders ++
  } else {
    operation = "receive"
    throttle = "0" // Receivers do not get throttled.
  }

  r := rn.get_router_by_name ( router_name )

  if r == nil {
    ume ( "Network: add_client: no such router: |%s|", router_name )
    return
  }

  // Clients just use the default version.
  ld_library_path := rn.Default_version.Ld_library_path
  pythonpath      := rn.Default_version.Pythonpath

  status_file := rn.log_path + "/" + name

  rn.client_status_files = append ( rn.client_status_files, status_file )

  c := client.New_client ( name,
                           config_path,
                           results_path,
                           operation,
                           r.Client_port ( ),
                           rn.client_path,
                           ld_library_path,
                           pythonpath,
                           status_file,
                           n_messages,
                           max_message_length,
                           throttle,
                           rn.verbose,
                           delay,
                           soak )

  rn.clients = append ( rn.clients, c )
}





func ( rn * Router_network ) Get_Client_By_Name ( target_name string ) ( * client.Client )  {
  for _, c := range rn.clients {
    if target_name == c.Name {
      return c
    }
  }
  return nil
}





func ( rn * Router_network ) Add_Address_To_Client ( client_name string,
                                                     addr        string ) {
  c := rn.Get_Client_By_Name ( client_name )
  if c == nil {
    ume ( "router_network: can't find client |%s|", client_name )
    return
  }

  c.Add_Address ( addr )
}





/*
  Connect the first router to the second. I.e. the first router
  will have a connector created in its config file that will 
  connect to the appropriate port of the second router.
  You cannot connect to an edge router.
*/
func ( rn * Router_network ) Connect_router ( router_1_name string, router_2_name string ) {
  router_1 := rn.get_router_by_name ( router_1_name )
  router_2 := rn.get_router_by_name ( router_2_name )

  if router_2.Type() == "edge" {
    // A router can't connect to an edge router.
    return
  }

  connect_to_port := router_2.Router_port()
  if router_1.Type() == "edge" {
    connect_to_port = router_2.Edge_port()
  }

  // Tell router_1 whom to connect to.  ( To whom to connect? To? )
  router_1.Connect_to ( router_2_name, connect_to_port )
  // And tell router_2 who just connected to it.
  router_2.Connected_to_you ( router_1_name, "edge" == router_1.Type() )
}


func ( rn * Router_network ) Are_connected ( router_1_name string, router_2_name string ) ( bool ) {

  router_1 := rn.get_router_by_name ( router_1_name )
  return router_1.Is_connected_to ( router_2_name )
}





/*
  Initialize the network. This is usually called once just before
  starting the network, but can also be called when the network is 
  running, after new routers have been added.
  And uninitialized routers will be initialized, i.e. their config
  files will be created, so they will be ready to start.
*/
func ( rn * Router_network ) Init ( ) {
  for _, router := range rn.routers {
    router.Init ( )
  }
  
  umi ( rn.verbose, "Network is initialized." )
  if rn.init_only {
    umi ( rn.verbose, "Init only is set : halting." )
    os.Exit ( 0 )
  }
}





/*
  Start all routers in the network that are not already started.
*/
func ( rn * Router_network ) Run ( ) {

  if rn.Failsafe > 0 {
    rn.failsafe_timer = time.NewTicker ( time.Duration(rn.Failsafe) * time.Second )
    go rn.failsafe_halt()
  }

  router_run_count := 0

  for _, r := range rn.routers {
    if r.State() == "initialized" {
      r.Run ( )
      router_run_count ++
    }
  }

  if len(rn.clients) > 0 {
    if router_run_count > 0 {
      nap_time := 5
      if rn.verbose {
        fp ( os.Stdout, 
             "    network info: sleeping %d seconds to wait for network stabilization.\n", 
             nap_time )
      }
      time.Sleep ( time.Duration(nap_time) * time.Second )
    }

    for _, c := range rn.clients {
      c.Run ( )
    }
  }

  rn.Running = true
}





func ( rn * Router_network ) Start_client_status_check  ( ticker_frequency int ) {
  rn.ticker_frequency = ticker_frequency

  if ticker_frequency <= 0 {
    ume ( "router_network: ticker_frequency must be > 0." )
    return
  }

  ticker_time      := time.Second * time.Duration ( rn.ticker_frequency )
  rn.client_ticker  = time.NewTicker ( ticker_time )

  go rn.Client_status_check ( )
}




func ( rn * Router_network ) failsafe_halt ( ) {
  for _ = range rn.failsafe_timer.C {
    umi ( rn.verbose, "network halting: failsafe." )
    rn.channel <- "failsafe"
  }
}





func ( rn * Router_network ) Client_status_check ( ) ( int ) {

  total_received := 0
  total_accepted := 0
  total_rejected := 0
  total_released := 0
  total_modified := 0


  for index, file_name := range rn.client_status_files {
    client := rn.clients [ index ]
    // I only care about senders, because only they have
    // access to information about all the messages.
    if strings.HasPrefix ( client.Name, "send") {
      if ! client.Completed {
        client.Completed = rn.read_client_status_file ( client.Name, file_name )

        total_received += client.Received
        total_accepted += client.Accepted
        total_rejected += client.Rejected
        total_released += client.Released
        total_modified += client.Modified
      }
    }
  }

  // Once per timer expiration.
  umi ( rn.verbose, 
        "client_status_check : received: %d accepted: %d rejected: %d released: %d modified: %d\n", 
        total_received, 
        total_accepted, 
        total_rejected, 
        total_released, 
        total_modified )

  return total_accepted + total_rejected + total_released + total_modified
}





func ( rn * Router_network ) Client_port ( target_router_name string ) ( client_port string ) {
  r := rn.get_router_by_name ( target_router_name )
  return r.Client_port ( )
}





func halt_router ( wg * sync.WaitGroup, r * router.Router ) {
  defer wg.Done()
  err := r.Halt ( )
  if err != nil {
    ume ( "Router %s halting error: %s", r.Name(), err.Error() )
  }
}





func (rn * Router_network) Halt_router ( router_name string ) ( error ) {
  r := rn.get_router_by_name ( router_name )
  if r == nil {
    return errors.New ( "No such router." )
  }

  go r.Halt()
  return nil
}





func (rn * Router_network) Halt_and_restart_router ( router_name string, pause int ) ( error ) {
  r := rn.get_router_by_name ( router_name )
  if r == nil {
    return errors.New ( "No such router." )
  }

  r.Halt()
  if rn.verbose {
    umi ( rn.verbose, "Halt_and_restart_router: Pausing %d seconds.", pause )
  }
  time.Sleep ( time.Duration(pause) * time.Second )

  r.Run ( )

  return nil
}





func (rn * Router_network) Get_edge_list ( ) ( edge_list [] string) {
  for _, r := range rn.routers {
    if r.Type() == "edge" {
      edge_list = append ( edge_list, r.Name() )
    }
  }

  return edge_list
}





func halt_client ( wg * sync.WaitGroup, c * client.Client ) {
  defer wg.Done()
  err := c.Halt ( )
  if err != nil && err.Error() != "process self-terminated." {
    ume ( "Client |%s| halting error: %s", c.Name, err.Error() )
  }
}





/*
  It takes a while to halt each router, so use a workgroup of
  goroutines to do them all in parallel.
*/
func ( rn * Router_network ) Halt ( ) {
  var wg sync.WaitGroup

  for _, c := range rn.clients {
    wg.Add ( 1 )
    go halt_client ( & wg, c )
  }

  for _, r := range rn.routers {
    if r.Is_not_halted() {
      wg.Add ( 1 )
      go halt_router ( & wg, r )
    }
  }

  wg.Wait()
  rn.Running = false
}




func ( rn * Router_network ) Display_routers ( ) {
  for index, r := range rn.routers {
    umi ( rn.verbose, "router %d: %s %d %s", index, r.Name(), r.Pid(), r.State() )
  }
}



func ( rn * Router_network ) Halt_first_edge ( ) error {
  
  for _, r := range rn.routers {
    if "edge" == r.Type() {
      if r.State() == "running" {
        if rn.verbose {
          umi ( rn.verbose, "halting router |%s|", r.Name() )
        }
        err := r.Halt ( )
        if err != nil {
          umi ( rn.verbose, "error halting router |%s| : |%s|\n", r.Name(), err.Error() )
        }
        return err
      }
    }
  }

  return errors.New ( "Router_network.Halt_first_edge error : Could not find an edge router to halt." )
}





func ( rn * Router_network ) get_router_by_name ( target_name string ) * router.Router {
  for _, router := range rn.routers {
    if router.Name() == target_name {
      return router
    }
  }

  ume ( "router_network: get_router_by_name: no such router |%s|", target_name )
  fp ( os.Stdout, "    routers are:\n" )
  for _, router := range rn.routers {
    fp ( os.Stdout, "      %s\n", router.Name() )
  }
  os.Exit ( 1 )
  return nil
}





func ( rn * Router_network ) How_many_interior_routers ( ) ( int ) {
  count := 0

  for _, r := range rn.routers {
    if r.Is_interior() {
      count ++
    }
  }

  return count
}





func ( rn * Router_network ) Get_nth_interior_router_name ( index int ) ( string ) {
  count := 0

  for _, r := range rn.routers {
    if r.Is_interior() {
      if count == index {
        return r.Name()
      }
      count ++
    }
  }
  return ""
}





func ( rn * Router_network ) read_client_status_file ( client_name, file_name string ) ( bool ) {

  is_it_completed := false

  client := rn.get_client_by_name ( client_name )
  if client == nil {
    ume ( "router_network: read_client_status_file: no such client |%s|", client_name )
    return is_it_completed
  }

  buf, err := ioutil.ReadFile ( file_name )
  if err != nil {
    // The client file does not exist yet.
    return is_it_completed
  }
  lines := strings.Split ( string(buf), "\n" )

  // Find the last line in the file that is not empty.
  var line string
  for index := len ( lines ) - 1; index >= 0; index -- {
    line = lines [ index ]
    if len(line) > 0 {
      break
    }
  }

  reader := strings.NewReader ( line )
  var ( timestamp, first_word string )
  _, err = fmt.Fscanf ( reader, 
                        "%s%s", 
                        & timestamp, 
                        & first_word )
  if err != nil {
    // In some conntexts, an error here is normal.
    return is_it_completed
  }

  if first_word == "report" {
    // example of a report-line from the client
    // 1553527855.073151  report received 0 accepted 0 rejected 0 released 100 modified 0
    reader := strings.NewReader ( line )
    var ( received,
          accepted,
          rejected,
          released,
          modified int ) 
    _, err = fmt.Fscanf ( reader,
                          "%s report received %d accepted %d rejected %d released %d modified %d",
                          & timestamp,
                          & received,
                          & accepted,
                          & rejected,
                          & released,
                          & modified )

    client.Received = received
    client.Accepted = accepted
    client.Rejected = rejected
    client.Released = released
    client.Modified = modified

    if err != nil {
      // In some conntexts, an error here is normal.
      return is_it_completed
    }
    total_messages_accounted_for := received + accepted + rejected + released + modified
    if total_messages_accounted_for >= client.N_messages {
      rn.completed_clients ++
      is_it_completed = true
    }
  }

  if rn.completed_clients >= rn.n_senders {
    // debug the failsafe by not halting here.
    // return is_it_completed
    umi ( true, "network: all %d senders have successfully completed.", rn.n_senders )
    umi ( rn.verbose, "halting network.\n" )
    // rn.client_ticker.Stop()
    // rn.channel <- "success"
  }

  return is_it_completed
}





func element_of ( target_str string, strings [] string ) ( bool ) {
  for _, str := range strings {
    if target_str == str {
      return true
    }
  }

  return false
}





func union ( list_1, list_2 [] string ) ( [] string ) {

  var union_list [] string

  for _, str := range list_1 {
    if ! element_of ( str, union_list ) {
      union_list = append ( union_list, str )
    }
  }

  for _, str := range list_2 {
    if ! element_of ( str, union_list ) {
      union_list = append ( union_list, str )
    }
  }

  return union_list 
}





func print_list ( label string, list [] string ) {
  fp ( os.Stdout, "%s\n", label )
  for _, x := range list {
    fp ( os.Stdout, "    %s\n", x )
  }
}





func ( rn * Router_network ) Is_the_network_connected ( ) ( bool ) {

  if len ( rn.routers ) <= 0  {
    return false
  }

  // reachable_nodes is the Big Kahuna. 
  // This is the list we are building up such that, when it is 
  // the same size as the set of all nodes -- that means that the
  // network is connected.
  reachable_nodes := [ ] string { "A" }
  var next_generation, neighbors_of_this_node [ ] string

  if len(reachable_nodes) >= len(rn.routers) {
    return true
  }

  for {
    next_generation = nil

    // Make the next generation.
    // For each node in the reachables, add all the nodes that
    // are connected to them into the new set of reachables.
    for _, reachable_node_name := range reachable_nodes {
      r := rn.get_router_by_name ( reachable_node_name )
      neighbors_of_this_node = union ( r.I_connect_to_names, r.Connect_to_me_interior )

      // For each neighbor of this node, put it into the next gen if
      //  1. it's not already there, and
      //  2. it's also not in the grand list of reachables already.
      for _, neighbor_of_this_node := range neighbors_of_this_node {
        if element_of ( neighbor_of_this_node, next_generation ) {
          continue
        }

        if element_of ( neighbor_of_this_node, reachable_nodes ) {
          continue
        }

        next_generation = append ( next_generation, neighbor_of_this_node )
      }
    }

    // Now we have built the next generation: the list of all nodes 
    // that are reachable from the set of 'reachable nodes' but were
    // not already contained therein.

    // If at this point the next generation is empty, the set 
    // of reachable nodes will not grow anymore. Fail.
    if len(next_generation) <= 0 {
      return false
    }
    
    // OK, so we *do* have some new nodes in the next generation.
    // Union them in with the set of reachables.
    reachable_nodes = union ( reachable_nodes, next_generation )

    // Do we have a Full House?
    // If so, then the network is connected!
    if len ( reachable_nodes ) >= len ( rn.routers ) {
      return true
    }
  }
}






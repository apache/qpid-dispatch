package main

import (
  "bufio"
  "fmt"
  "os"
  "regexp"
  "time"
  // "errors"

  "utils"
  rn "router_network"
  "lisp"
)



var fp  = fmt.Fprintf
var ume = utils.M_error
var umi = utils.M_info




// Normally, when the user starts up Mercury, a new session
// is created. It has its own directory, under which is
// captured all information necessary to reproduce what 
// happened during this session.
type Session struct {
  // The name of the session will determine the paths.
  // It will be unique, as long as you don't start another
  // session within the same minute of the same day.
  name          string

  // Where the config files and environment variables and
  // start commands of he routers and clients are stored.
  config_path   string      

  // Where the log files of routers and clients for this 
  // session are stored.
  log_path      string       

  // Results are files probably written by clients, showing
  // measurements we want to save, such as message flight times.
  // This will just be the directory name. The clients are 
  // responsible for making sure that the names of the individual 
  // files do not collide.
  results_path   string
}





func new_session ( ) ( * Session ) {
  cwd, err := os.Getwd()
  if err != nil {
    ume ( "Can't get cwd path for program name %s", os.Args[0] )
    os.Exit ( 1 )
  }

  name := cwd + "/sessions/session_" + time.Now().Format ( "2006_01_02_1504" )
  s := & Session { name         : name,
                   config_path  : name + "/config",
                   log_path     : name + "/logs",
                   results_path : name + "/results" }

  utils.Find_or_create_dir ( s.config_path )
  utils.Find_or_create_dir ( s.log_path )
  utils.Find_or_create_dir ( s.results_path )

  fp ( os.Stdout, "Results dir is |%s|\n", s.results_path )

  return s
}




// When the user calls a command from the command line,
// this is the type of function that actually does the
// work.
type command_fn func ( * Merc, * lisp.List, string )


//---------------------------------------------------
// This is an argument that can be provided 
// by the user to a command.
//---------------------------------------------------
type arg_descriptor struct {
  name          string // The label of the arg, if it appears.
  unlabelable   bool   // Can it be unlabeled?
  data_type     string // How should the value be interpreted?
  default_value string // If user does not specify, it gets this value.
  help          string // Help string to show the user.
  explicit      bool   // Did the caller explicity specify? (Or was this default?)

  // All fields below this point are filled in by a particular
  // instance of this command. I.e., when the user types it on
  // the command line.

  string_value  string // The string that the caller gave.
  int_value     int    // The integer value (if any) of the string.
  list_value    * lisp.List
}



//---------------------------------------------------
// This is a command that the user can call from
// the command line.
// Each command has a maximum of two magic args that
// can be unlabeled. One an int, 
// and the other a string.
//---------------------------------------------------
type command struct {
  name                    string
  fn                      command_fn
  help                    string

  // Above this line are all the fields provided by the
  // caller when the command is first created. Below are
  // fields that are filled in later, as args are added.
  argmap                  map [ string ] * arg_descriptor
  unlabelable_int       * arg_descriptor
  unlabelable_string    * arg_descriptor
}





// Mercury Context
type Merc struct {
  session                  * Session

  verbose                    bool
  echo                       bool
  prompt                     bool

  n_worker_threads           int

  network                  * rn.Router_network
  network_running            bool
  receiver_count             int
  sender_count               int

  // This counts total edges that have been made on any
  // router anywhere, just so we can give a unique name 
  // to each.
  edge_count                 int

  mercury_log_name           string
  mercury_log_file         * os.File
  first_nonwhitespace_rgx  * regexp.Regexp
  line_rgx                 * regexp.Regexp
  
  commands                   map [ string ] * command
  versions              [] * rn.Version
  default_version          * rn.Version

  cpu_freqs             []   string
}






/*=====================================================================
  Helper Functions
======================================================================*/



func ( merc * Merc ) add_version ( v * rn.Version ) {
  merc.versions = append ( merc.versions, v )
  if len(merc.versions) == 1 { 
    umi ( merc.verbose, "add_version: There is now 1 version.\n" )
    merc.default_version = v
    umi ( merc.verbose, "add_version: default version is |%s|\n", merc.default_version.Name )
  } else {
    umi ( merc.verbose, "add_version: There are now %d versions.\n", len(merc.versions) )
  }
}





func ( merc * Merc ) add_command ( name  string, 
                                   fn    command_fn,
                                   help  string ) ( * command ) {
  cmd := & command { name : name,
                     fn   : fn,
                     help : help }
  cmd.argmap = make ( map [ string ] * arg_descriptor, 0 )

  merc.commands [ name ] = cmd
  return cmd
}





func (c * command) add_arg ( name          string,
                             unlabelable   bool,
                             data_type     string,
                             default_value string,
                             help          string ) {
  a := & arg_descriptor { name          : name,
                          unlabelable   : unlabelable,
                          data_type     : data_type,
                          default_value : default_value,
                          help          : help }

  // If this arg is one of the two allowable unlabelables,
  // make sure that the spot it wants is not already filled.
  if unlabelable {
    if data_type == "string" {
      if c.unlabelable_string != nil {
        ume ( "command |%s| already has an unlabelable string arg: |%s|", 
              c.name, 
              c.unlabelable_string.name )
        return
      }
      c.unlabelable_string = a
    } else if data_type == "int" {
      if c.unlabelable_int != nil {
        ume ( "command |%s| already has an unlabelable int arg: |%s|",
              c.name,
              c.unlabelable_int.name )
        return
      }
      c.unlabelable_int = a
    } else {
      ume ( "add_arg: unknown arg data type: |%s|", data_type )
      return
    }
  }

  c.argmap [ name ] = a
}





func call_command ( merc * Merc, command_line * lisp.List, original_line string ) {
  cmd_name, err := command_line.Get_atom ( 0 ) 
  if err != nil {
    fp ( os.Stdout, "\n--------------------------------------\n" )
    fp ( os.Stdout, "    %c error: call_command: |%s|\n", mercury, err.Error() )
    fp ( os.Stdout, "--------------------------------------\n\n" )
    return
  }

  cmd := merc.commands [ cmd_name ]
  if cmd == nil {
    fp ( os.Stdout, "\n--------------------------------------\n" )
    fp ( os.Stdout, "    %c error: no such command: |%s|\n", mercury, cmd_name )
    fp ( os.Stdout, "--------------------------------------\n\n" )
    help ( merc, nil, "" )
    return
  }

  cmd.fn ( merc, command_line, original_line )
}





func new_merc ( ) ( merc * Merc ) {
  merc = & Merc { verbose          : false,
                  network_running  : false,
                  n_worker_threads : 4,
                  line_rgx         : regexp.MustCompile(`\s+`),
                  first_nonwhitespace_rgx : regexp.MustCompile(`\S`) }
  return merc
}





func get_next_interior_router_name ( merc * Merc ) ( string ) {
  routers_so_far := merc.network.Get_n_interior_routers()
  name := fmt.Sprintf ( "%c", 'A' + byte(routers_so_far) )
  return name
}





func this_is_an_interior_router_name ( merc * Merc, name string ) ( bool ) {
  routers_so_far := merc.network.Get_n_interior_routers()
  byte_array := []byte(name)
  router_name_byte := byte_array[0] 

  if 'A' <= router_name_byte &&  router_name_byte <= 'Z' {
    this_router_index := byte_array[0] - 'A'
    if int(this_router_index) < routers_so_far {
      return true
    }
  }

  return false
}





func listen_for_network_halt ( merc * Merc, channel chan string ) {
  msg := <- channel
  umi ( merc.verbose, "network has halted.\n" )

  result_file_name := merc.session.name + "/result"
  result_file, _ := os.Create ( result_file_name )
  if result_file != nil {
    defer result_file.Close() 
    fmt.Fprintf ( result_file, "%s\n", msg )
  }
}





/*=====================================================================
  Main
======================================================================*/


func main ( ) {

  mercury_root := os.Getenv ( "MERCURY_ROOT" )

  merc := new_merc ( )

  // NOTE: to run this code, your username needs
  // to have sudo NOPASSWD privileges,
  // and you need to have installed cpufrequtils.
  // i.e.   dnf install cpufrequtils
  merc.cpu_freqs = utils.Get_CPU_freqs ( )
  fp ( os.Stdout, "main: cpu freqs: \n" )
  for _, freq := range merc.cpu_freqs {
    fp ( os.Stdout, "   %s\n", freq )
  }

  // Put this outside of new_merc because in future we 
  // might want the choice of loading a session, based
  // on command line arg.
  merc.session = new_session()
  fp ( os.Stdout, " session name: |%s|\n", merc.session.name )
  utils.Find_or_create_dir ( merc.session.name )
  merc.mercury_log_name = merc.session.name + "/mercury_log"
  merc.mercury_log_file, _ = os.Create ( merc.mercury_log_name )

  network_channel := make ( chan string )
  merc.network = rn.New_router_network ( "network", 
                                         mercury_root,
                                         merc.session.log_path,
                                         network_channel )


  // Set a default results path here. 
  // SOme commands may want to replace this with their own.
  // If they do, then any clients made after that moment
  // will use their path.
  merc.network.Set_results_path ( merc.session.name + "/results" )

  // In the background, listen for the network telling us 
  // that it has completed. (This happens if it is runs a test 
  // successfully.)
  // TODO  Gotta think about this.
  // go listen_for_network_halt ( merc, network_channel )

  /*===========================================
    Make commands. 
  ============================================*/

  merc.commands = make ( map[string] * command, 0 )



  // seed command -------------------------------------------------------
  cmd := merc.add_command ( "seed",
                             seed,
                            "Seed the random number generator." )
  cmd.add_arg ( "value",
                true,        // unlabelable
                "int",
                "0",
                "Seed value for the random number generator." )


  // verbose command -------------------------------------------------------
  cmd = merc.add_command ( "verbose",
                            verbose,
                           "Turn verbosity 'on' or 'off'." )
  cmd.add_arg ( "state",
                true,        // unlabelable
                "string",
                "on",
                "'on' or 'off" )


  // echo command -------------------------------------------------------
  cmd = merc.add_command ( "echo_all",
                            echo_all,
                           "Echo all non-blank command lines." )
  cmd.add_arg ( "state",
                true,        // unlabelable
                "string",
                "on",
                "'on' or 'off" )


  // prompt command -------------------------------------------------------
  cmd = merc.add_command ( "prompt",
                            prompt,
                           "Prompt after every command before continuing." )
  cmd.add_arg ( "state",
                true,        // unlabelable
                "string",
                "on",
                "'on' or 'off" )


  // version_roots -------------------------------------------------------
  cmd = merc.add_command ( "version_roots",
                            version_roots,
                            "Define a code-version by providing root dirs from which paths are calculated." )
  cmd.add_arg ( "name",
                false,        // not unlabelable
                "string",
                "",
                "Name of this version." )

  cmd.add_arg ( "dispatch",
                false,        // not unlabelable
                "string",
                "",
                "Dispatch install directory." )

  cmd.add_arg ( "proton",
                false,        // not unlabelable
                "string",
                "",
                "Proton install directory." )



  // routers command -------------------------------------------------------
  cmd = merc.add_command ( "routers",
                            routers,
                           "Create new routers." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "3",    // default is 3 routers
                "How many new (and unconnected) routers to create." )

  cmd.add_arg ( "version",
                true,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )



  // connect command -------------------------------------------------------
  cmd = merc.add_command ( "connect",
                            connect,
                           "Connect two routers. Example: connect A B" )
  // The connect command uses its own command line processing.



  // echo command -------------------------------------------------------
  cmd = merc.add_command ( "echo",
                            echo,
                           "Echo line to console." )
  // The echo command uses its own command line processing.



  //=======================================================================
  // End Topology Commands.
  //=======================================================================


  // linear command -------------------------------------------------------
  cmd = merc.add_command ( "linear",
                            linear,
                           "Create a linear router network." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "3",    // default is 3 routers
                "How many routers to create in the linear network." )

  cmd.add_arg ( "version",
                true,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )



  // mesh command -------------------------------------------------------
  cmd = merc.add_command ( "mesh",
                            mesh,
                           "Create a fully-connected router network." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "4",    // default is 4 routers
                "How many routers to create in the mesh network." )

  cmd.add_arg ( "version",
                true,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )



  // teds_diamond command -------------------------------------------------------
  cmd = merc.add_command ( "teds_diamond",
                            teds_diamond,
                           "Create a fully-connected router network, with two outliers." )
  cmd.add_arg ( "version",
                true,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )



  // ring command -------------------------------------------------------
  cmd = merc.add_command ( "ring",
                            ring,
                           "Create a ring-shaped router network." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "4",    // default is 4 routers
                "How many routers to create in the ring network." )

  cmd.add_arg ( "version",
                true,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )


  // star command -------------------------------------------------------
  cmd = merc.add_command ( "star",
                            star,
                           "Create a star-shaped router network." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "4",    // default is 4 routers
                "How many routers to create in the star network." )

  cmd.add_arg ( "version",
                true,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )


  // random_network command -------------------------------------------------------
  cmd = merc.add_command ( "random_network",
                            random_network,
                           "Create a randomly-connected network wuth the requested number of routers." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "4",    // default is 4 routers
                "How many routers to create in the random network." )

  cmd.add_arg ( "version",
                true,   // unlabelable
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )



  //=======================================================================
  // End Topology Commands.
  //=======================================================================




  // edges command -------------------------------------------------------
  cmd = merc.add_command ( "edges",
                            edges,
                           "Create edge router on a given interior router." )
  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "",
                "How many edge routers to create." )

  cmd.add_arg ( "router",
                true,   // unlabelable
                "string",
                "",
                "Which interior router to add the edges to." )

  cmd.add_arg ( "version",
                false,
                "string",
                "",
                "Which version of the dispatch code to use. Defaults to the first version you defined." )


  // send command -------------------------------------------------------
  cmd = merc.add_command ( "send",
                            send,
                           "Create message-sending clients." )

  cmd.add_arg ( "router",
                true,   // unlabelable
                "string",
                "",
                "Which router the senders should attach to." )

  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "1",
                "How many senders to make." )

  cmd.add_arg ( "n_messages",
                false,
                "int",
                "1000",
                "How many messages to send." )

  cmd.add_arg ( "max_message_length",
                false,
                "int",
                "1000",
                "Max length for each messages. " +
                "Lengths will be random, and average will be half this." )

  cmd.add_arg ( "edges",
                false,
                "string",
                "",
                "Add senders to the edges of this router. " +
                "i.e. 'edges A' means add senders to edges of router A." )

  cmd.add_arg ( "throttle",
                false,
                "string",    // Just ... don't ask.
                "0",
                "How many msec between each sent message. " +
                "0 means send as fast as possible." )

  cmd.add_arg ( "address",
                false,
                "string",
                "my_address",
                "Address to send to. Embed a '%d' if you " +
                "want addresses to count up." )

  cmd.add_arg ( "start_at",
                false,
                "int",
                "1",
                "If you use %d in address, use this to tell what int " +
                "the counting should start with." )

  cmd.add_arg ( "apc",
                false,
                "int",
                "1",
                "Addresses per client. Makes each sender have N addresses." )

  cmd.add_arg ( "cpa",
                false,
                "int",
                "1",
                "Clients per address. Makes each address shared by N clients." )

  cmd.add_arg ( "delay",
                false,
                "string",
                "0",
                "How many seconds each sender should wait before starting to send." )


  // recv command -------------------------------------------------------
  cmd = merc.add_command ( "recv",
                            recv,
                           "Create message-receiving clients." )
  cmd.add_arg ( "router",
                true,   // unlabelable
                "string",
                "",
                "Which router the senders should attach to." )

  cmd.add_arg ( "count",
                true,   // unlabelable
                "int",
                "1",
                "How many senders to make." )

  cmd.add_arg ( "n_messages",
                false,
                "int",
                "1000",
                "How many messages to send." )

  cmd.add_arg ( "edges",
                false,
                "string",
                "",
                "Add receivers to the edges of this router. " +
                "i.e. 'edges A' means add senders to edges of router A." )

  cmd.add_arg ( "address",
                false,
                "string",
                "my_address",
                "Address to send to. Embed a '%d' if you " +
                "want addresses to count up." )

  cmd.add_arg ( "start_at",
                false,
                "int",
                "1",
                "If %d in address, this tells where to start." )

  cmd.add_arg ( "max_message_length",
                false,
                "int",
                "1000",
                "Max length for each messages. " )

  cmd.add_arg ( "apc",
                false,
                "int",
                "1",
                "Addresses per client. Makes each sender have N addresses." )

  cmd.add_arg ( "cpa",
                false,
                "int",
                "1",
                "Clients per address. Makes each address shared by N clients." )


  // run command -------------------------------------------------------
  cmd = merc.add_command ( "run",
                            run,
                           "Start the network of routers and clients." )


  // quit command -------------------------------------------------------
  cmd = merc.add_command ( "quit",
                            quit,
                           "Shut down the network and halt Mercury." )


  // console_ports command -------------------------------------------------------
  cmd = merc.add_command ( "console_ports",
                            console_ports,
                           "Show the console ports for all routers." )


  // inc command -------------------------------------------------------
  cmd = merc.add_command ( "inc",
                            inc,
                           "Include the named file into the command stream." )
  cmd.add_arg ( "file",
                true,       // unlabelable
                "string",   
                "",
                "Name of file to include." )




  // help command -------------------------------------------------------
  cmd = merc.add_command ( "help",
                            help,
                           "List all commands, or give help on a specific command." )


  // kill command -------------------------------------------------------
  cmd = merc.add_command ( "kill",
                            kill,
                           "Kill a router." )
  cmd.add_arg ( "router",
                true,   // unlabelable
                "string",
                "",
                "Which router to kill." )


  // kill_and_restart command -------------------------------------------------------
  cmd = merc.add_command ( "kill_and_restart",
                            kill_and_restart,
                           "Kill and restart a router, after a pause." )
  cmd.add_arg ( "router",
                true,   // unlabelable
                "string",
                "",
                "Which router to kill and restart." )

  cmd.add_arg ( "pause",
                true,   // unlabelable
                "int",
                "10",
                "How long to pause, in seconds, after killing and before restarting." )

  // sleep command -------------------------------------------------------
  cmd = merc.add_command ( "sleep",
                            sleep,
                           "Sleep for the given number of seconds." )
  cmd.add_arg ( "seconds",
                true,   // unlabelable
                "int",
                "3",
                "The number of seconds to sleep." )


  // start_client_status_check command -------------------------------------------------------
  cmd = merc.add_command ( "start_client_status_check",
                            start_client_status_check,
                           "Start checking on the status of clients every N seconds." )
  cmd.add_arg ( "seconds",
                true,   // unlabelable
                "int",
                "15",
                "The number of seconds between status checks." )


  // failsafe command -------------------------------------------------------
  cmd = merc.add_command ( "failsafe",
                            failsafe,
                           "Force a failure this many second after first client status check." )
  cmd.add_arg ( "seconds",
                true,   // unlabelable
                "int",
                "30",
                "The number of seconds until forced failure." )

  // wait_for_network command -------------------------------------------------------
  cmd = merc.add_command ( "wait_for_network",
                            wait_for_network,
                           "Wait for the network to settle down after being created or changed." )


  // example_test_1 command -------------------------------------------------------
  cmd = merc.add_command ( "example_test_1",
                            example_test_1,
                           "Macro: perform a small example test." )



  // reset command -------------------------------------------------------
  cmd = merc.add_command ( "reset",
                           reset,
                           "Restore Mercury to original conditions." )



  /*--------------------------------------------
    Process files named on command line.
  --------------------------------------------*/
  for i := 1; i < len(os.Args); i ++ {
    read_file ( merc, os.Args[i] )
  }

  /*--------------------------------------------
    Prompt for and read lines of input until
    the user tells us to quit.
  --------------------------------------------*/
  reader := bufio.NewReader ( os.Stdin )
  for {
    fp ( os.Stdout, "%c ", mercury )  // prompt
    line, _ := reader.ReadString ( '\n' )
    process_line ( merc, line )
  }
}






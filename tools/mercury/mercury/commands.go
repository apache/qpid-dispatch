package main

import (
  "fmt"
  "os"
  "sort"
  "strings"
  "math/rand"
  "time"

  "lisp"
)




/*=====================================================================
  Command Functions
======================================================================*/


func usage ( merc * Merc, command_name string ) {
  fp ( os.Stdout, "    usage for command |%s|\n", command_name )
}





func verbose ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "verbose" ]
  parse_command_line ( merc, cmd, command_line )

  val := cmd.unlabelable_string.string_value
  if val == "on" {
    merc.verbose = true
  } else if val == "off" {
    merc.verbose = false
  } else {
    ume ( "verbose: unknown value: |%s|", val )
  }

  merc.network.Verbose ( merc.verbose )
  umi ( merc.verbose, "verbose: set to |%t|", merc.verbose )
}





// This is the command that turns echo-all-commands on and off.
// There is another 'echo' command that causes just its own
// line to be echoed to the console.
func echo_all ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "echo" ]
  parse_command_line ( merc, cmd, command_line )

  val := cmd.unlabelable_string.string_value
  if val == "on" {
    merc.echo = true
  } else if val == "off" {
    merc.echo = false
  } else {
    ume ( "echo: unknown value: |%s|", val )
    return
  }

  umi ( merc.verbose, "echo: set to |%s|", val )
}





func prompt ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "prompt" ]
  parse_command_line ( merc, cmd, command_line )

  val := cmd.unlabelable_string.string_value
  if val == "on" {
    merc.prompt = true
  } else if val == "off" {
    merc.prompt = false
  } else {
    ume ( "prompt: unknown value: |%s|", val )
  }

  umi ( merc.verbose, "prompt: set to |%s|", val )
}





func edges ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "edges" ]
  parse_command_line ( merc, cmd, command_line )

  router_name       := cmd.unlabelable_string.string_value
  count             := cmd.unlabelable_int.int_value
  requested_version := cmd.argmap["version"].string_value

  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  } 

  // Make the edges.
  var edge_name string
  for i := 0; i < count; i ++ {
    if requested_version == "RANDOM" {
      version = merc.random_version_name()
    }
    merc.edge_count ++
    edge_name = fmt.Sprintf ( "edge_%04d", merc.edge_count )
    merc.network.Add_edge ( edge_name, 
                            version,
                            merc.session.config_path,
                            merc.session.log_path )
    merc.network.Connect_router ( edge_name, router_name )
    umi ( merc.verbose, 
          "edges: added edge %s with version %s to router %s", 
          edge_name, 
          version, 
          router_name )
  }
}





func seed ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "seed" ]
  parse_command_line ( merc, cmd, command_line )

  value := cmd.unlabelable_int.int_value
  rand.Seed ( int64 ( value ) )
}





func version_roots ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "version_roots" ]
  parse_command_line ( merc, cmd, command_line )

  name     := cmd.argmap [ "name" ]     . string_value
  proton   := cmd.argmap [ "proton" ]   . string_value
  dispatch := cmd.argmap [ "dispatch" ] . string_value

  if name == "" || proton == "" || dispatch == "" {
    help_for_command ( merc, cmd )
    return
  }

  merc.network.Add_version_with_roots ( name, proton, dispatch )
}





func send ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "send" ]
  parse_command_line ( merc, cmd, command_line )

  // The user may specify target routers two different ways:
  // with the 'router' arg, which specifies a single interior
  // node, or with the 'edges' arg, which specifies all the 
  // edge-routers attached to an interior router.
  // This list will accumulate all targets.
  target_router_list := make ( [] string, 0 )

  router_name  := cmd.unlabelable_string.string_value
  client_count := cmd.unlabelable_int.int_value

  if router_name != "" {
    target_router_list = append ( target_router_list, router_name )
  }
  router_with_edges := cmd.argmap [ "edges" ] . string_value

  var edge_list [] string
  if router_with_edges != "" {
    edge_list = merc.network.Get_router_edges ( router_with_edges )
  }
  target_router_list = append ( target_router_list, edge_list ... )

  // If it turns out that this address is not variable, 
  // then this 'final' address is the only one we will
  // use. But if address is variable this value will get
  // replaced every time through the loop with the changing
  // value of the variable address: add_r, addr_2, etc.
  address    := cmd.argmap [ "address" ] . string_value
  final_addr := address

  // Is this address variable? It is if it contains a "%d" somewhere.
  // I have to test for this because fmt.Sprintf treats it as an 
  // error if I have a format string that contains no "%d" and I try
  // to print it with what would be an unused int arg.
  variable_address := false
  if strings.Contains ( address, "%d" ) {
    variable_address = true
  }

  addr_number        := cmd.argmap [ "start_at"           ] . int_value
  n_messages         := cmd.argmap [ "n_messages"         ] . int_value
  max_message_length := cmd.argmap [ "max_message_length" ] . int_value
  throttle           := cmd.argmap [ "throttle"           ] . string_value
  apc                := cmd.argmap [ "apc"                ] . int_value
  cpa                := cmd.argmap [ "cpa"                ] . int_value
  delay              := cmd.argmap [ "delay"              ] . string_value

  if apc > 1 && ! variable_address {
    ume ( "send: can't have apc > 1 but no variable address.\n" )
    return
  }

  if apc < 1 || cpa < 1 {
    ume ( "send: can't have apc or cpa < 1.\n" )
    return
  }

  if apc != 1 && cpa != 1 {
    ume ( "send: can't have apc or cpa < 1.\n" )
    return
  }

  // This index refers to the list of target routers that was made, above.
  router_index := 0

  var sender_name,
      config_path string

  if apc > 1 {
    // We want multiple addresses per client.
    for i := 0; i < client_count; i ++ {
      merc.sender_count ++
      sender_name  = fmt.Sprintf ( "send_%04d", merc.sender_count )
      config_path  = merc.session.config_path  + "/clients/" + sender_name
      router_name  = target_router_list[router_index]

      router_index ++
      if router_index >= len(target_router_list) {
        router_index = 0
      }

      merc.network.Add_sender ( sender_name,
                                config_path,
                                n_messages,
                                max_message_length,
                                router_name,
                                throttle,
                                delay )
      umi ( merc.verbose,
            "send: added sender |%s| to router |%s|.",
            sender_name,
            router_name )

      // Do each address for this sender.
      for j := 0; j < apc; j ++ {
        final_addr = fmt.Sprintf ( address, addr_number )

        merc.network.Add_Address_To_Client ( sender_name, final_addr )
        addr_number ++

        umi ( merc.verbose,
              "send: added addr |%s| to sender |%s|.", 
              final_addr,
              sender_name )
      }
    }
  } else if cpa > 1 {
    // We want multiple clients per address.
    for i := 0; i < client_count; i ++ {
      // Only increment addr_number every CPA clinets.
      if i > 0 && 0 == (i % cpa) {
        addr_number ++
      }
      final_addr = fmt.Sprintf ( address, addr_number )

      merc.sender_count ++
      sender_name  = fmt.Sprintf ( "send_%04d", merc.sender_count )
      config_path  = merc.session.config_path + "/clients/" + sender_name
      router_name  = target_router_list[router_index]

      merc.network.Add_sender ( sender_name,
                                config_path,
                                n_messages,
                                max_message_length,
                                router_name,
                                throttle,
                                delay )

      merc.network.Add_Address_To_Client ( sender_name, final_addr )

      umi ( merc.verbose,
            "send: added sender |%s| with addr |%s| to router |%s|.",
            sender_name,
            final_addr,
            router_name )

      router_index ++
      if router_index >= len(target_router_list) {
        router_index = 0
      }
    }
  } else {
    // We want 1 address for each client.
    for i := 0; i < client_count; i ++ {

      // If the address is *not* variable, then final_addr 
      // already has the correect string.
      if variable_address {
        final_addr = fmt.Sprintf ( address, addr_number )
        addr_number ++
      }

      merc.sender_count ++
      sender_name  = fmt.Sprintf ( "send_%04d", merc.sender_count )
      config_path  = merc.session.config_path + "/clients/" + sender_name
      router_name  = target_router_list[router_index]

      merc.network.Add_sender ( sender_name,
                                config_path,
                                n_messages,
                                max_message_length,
                                router_name,
                                throttle,
                                delay )
      merc.network.Add_Address_To_Client ( sender_name, final_addr )

      umi ( merc.verbose,
            "send: added sender |%s| with addr |%s| to router |%s|.",
            sender_name,
            final_addr,
            router_name )

      router_index ++
      if router_index >= len(target_router_list) {
        router_index = 0
      }
    }
  }

}





func recv ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "recv" ]
  parse_command_line ( merc, cmd, command_line )

  // The user may specify target routers two different ways:
  // with the 'router' arg, which specifies a single interior
  // node, or with the 'edges' arg, which specifies all the 
  // edge-routers attached to an interior router.
  // This list will accumulate all targets.
  target_router_list := make ( [] string, 0 )

  router_name  := cmd.unlabelable_string.string_value
  client_count := cmd.unlabelable_int.int_value

  if router_name != "" {
    target_router_list = append ( target_router_list, router_name )
  }
  router_with_edges := cmd.argmap [ "edges" ] . string_value

  var edge_list [] string
  if router_with_edges != "" {
    edge_list = merc.network.Get_router_edges ( router_with_edges )
  }
  target_router_list = append ( target_router_list, edge_list ... )

  // At this point we are done adding routers to the target routers list.
  // Soon we will start to use it.
  // If it's still empty, that is Bad.
  if len ( target_router_list ) <= 0 {
    ume ( "recv: target routers list is empty.\n" )
    return
  }

  // If it turns out that this address is not variable, 
  // then this 'final' address is the only one we will
  // use. But if address is variable this value will get
  // replaced every time through the loop with the changing
  // value of the variable address: add_r, addr_2, etc.
  address    := cmd.argmap [ "address" ] . string_value
  final_addr := address

  // Is this address variable? It is if it contains a "%d" somewhere.
  // I have to test for this because fmt.Sprintf treats it as an 
  // error if I have a format string that contains no "%d" and I try
  // to print it with what would be an unused int arg.
  variable_address := false
  if strings.Contains ( address, "%d" ) {
    variable_address = true
  }

  addr_number        := cmd.argmap [ "start_at"           ] . int_value
  n_messages         := cmd.argmap [ "n_messages"         ] . int_value
  max_message_length := cmd.argmap [ "max_message_length" ] . int_value
  apc                := cmd.argmap [ "apc"                ] . int_value
  cpa                := cmd.argmap [ "cpa"                ] . int_value

  if apc > 1 && ! variable_address {
    ume ( "recv: can't have apc > 1 but no variable address.\n" )
    return
  }

  if apc < 1 || cpa < 1 {
    ume ( "recv: can't have apc or cpa < 1.\n" )
    return
  }

  if apc != 1 && cpa != 1 {
    ume ( "recv: can't have apc or cpa < 1.\n" )
    return
  }

  // This index refers to the list of target routers that was made, above.
  router_index := 0

  var receiver_name,
      config_path string

  if apc > 1 {
    // We want multiple addresses per client.
    for i := 0; i < client_count; i ++ {
      merc.receiver_count ++
      receiver_name = fmt.Sprintf ( "recv_%04d", merc.receiver_count )
      config_path = merc.session.config_path  + "/clients/" + receiver_name
      router_name = target_router_list[router_index]
      merc.network.Add_receiver ( receiver_name,
                                  config_path,
                                  n_messages,
                                  max_message_length,
                                  router_name )
      umi ( merc.verbose,
            "recv: added receiver |%s| to router |%s|.", 
            receiver_name,
            router_name )
      router_index ++
      if router_index >= len(target_router_list) {
        router_index = 0

      // Do each address for this receiver.
      for j := 0; j < apc; j ++ {
        final_addr = fmt.Sprintf ( address, addr_number )
        addr_number ++
        
        merc.network.Add_Address_To_Client ( receiver_name, final_addr )

        umi ( merc.verbose,
              "recv: added address |%s| to receiver |%s|.", 
              final_addr,
              receiver_name )
        }
      }
    }
  } else if cpa > 1 {
    // We want multiple clients per address.
    for i := 0; i < client_count; i ++ {

      final_addr = fmt.Sprintf ( address, addr_number )
      // Only increment addr_number every CPA clinets.
      if i > 0 && 0 == (i % cpa) {
        addr_number ++
      }

      merc.receiver_count ++
      receiver_name = fmt.Sprintf ( "recv_%04d", merc.receiver_count )
      config_path   = merc.session.config_path + "/clients/" + receiver_name
      router_name   = target_router_list[router_index]

      merc.network.Add_receiver ( receiver_name,
                                  config_path,
                                  n_messages,
                                  max_message_length,
                                  router_name )
      merc.network.Add_Address_To_Client ( receiver_name, final_addr )
                                  

      umi ( merc.verbose,
            "recv: added receiver |%s| with addr |%s| to router |%s|.",
            receiver_name,
            final_addr,
            router_name )

      router_index ++
      if router_index >= len(target_router_list) {
        router_index = 0
      }
    }
  } else {
    // We want 1 address for each client.
    for i := 0; i < client_count; i ++ {

      // If the address is *not* variable, then final_addr 
      // already has the correect string.
      if variable_address {
        final_addr = fmt.Sprintf ( address, addr_number )
        addr_number ++
      }

      merc.receiver_count ++
      receiver_name = fmt.Sprintf ( "recv_%04d", merc.receiver_count )
      config_path   = merc.session.config_path  + "/clients/" + receiver_name
      router_name   = target_router_list[router_index]

      merc.network.Add_receiver ( receiver_name,
                                  config_path,
                                  n_messages,
                                  max_message_length,
                                  router_name )
      merc.network.Add_Address_To_Client ( receiver_name, final_addr )


      umi ( merc.verbose,
            "recv: added receiver |%s| with addr |%s| to router |%s|.",
            receiver_name,
            final_addr,
            router_name )

      router_index ++
      if router_index >= len(target_router_list) {
        router_index = 0
      }
    }
  }
}





func routers ( merc  * Merc, command_line * lisp.List, _ string ) {
  if len(merc.network.Versions) < 1 {
    ume ( "routers: You must define at least one version before creating routers." )
    return
  }

  cmd := merc.commands [ "routers" ]
  parse_command_line ( merc, cmd, command_line )

  count             := cmd.unlabelable_int.int_value
  requested_version := cmd.unlabelable_string.string_value

  // If no version name was supplied, use default.
  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  }

  // Make the requested routers.
  var router_name string
  for i := 0; i < count; i ++ {
    router_name = get_next_interior_router_name ( merc )

    if requested_version == "RANDOM" {
      version = merc.random_version_name()
    }

    merc.network.Add_router ( router_name, 
                              version, 
                              merc.session.config_path,
                              merc.session.log_path )
    umi ( merc.verbose, 
          "routers: added router |%s| with version |%s|.", 
          router_name, 
          version )
  }
}





func connect ( merc  * Merc, command_line * lisp.List, _ string ) {
  from_router, _ := command_line.Get_atom ( 1 )
  to_router, _   := command_line.Get_atom ( 2 )

  if from_router == "" || to_router == "" {
    ume ( "connect: from and to routers must both be specified." )
    return
  }

  merc.network.Connect_router ( from_router, to_router )
  
  umi ( merc.verbose, 
        "connect: connected router |%s| to router |%s|.", 
        from_router, 
        to_router )
}





func echo ( merc  * Merc, command_line * lisp.List, original_line string ) {
  start_echo_at := strings.Index ( original_line, "echo" ) + 5
  if start_echo_at >= len(original_line) {
    fp ( os.Stdout, "\n" )
    return
  }

  fp ( os.Stdout, "%s\n", original_line [ start_echo_at : ] )
}





func inc ( merc  * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "inc" ]
  parse_command_line ( merc, cmd, command_line )

  file_name := cmd.unlabelable_string.string_value

  if file_name == "" {
    ume ( "inc: I need a file name." )
    return
  }

  if merc.verbose {
    umi ( merc.verbose,
          "inc: |%s|", 
          file_name )
  }

  read_file ( merc, file_name )
}





func ( merc * Merc ) random_version_name ( ) (string) {
  n_versions   := len(merc.network.Versions)
  random_index := rand.Intn ( n_versions )
  return merc.network.Versions[random_index].Name
}





func ( merc * Merc ) random_router_name ( ) (string) {
  interior_router_names := merc.network.Get_interior_routers_names ( )
  n_routers    := len ( interior_router_names )
  random_index := rand.Intn ( n_routers )
  return interior_router_names [ random_index ]
}





func ( merc * Merc ) make_random_connection ( ) ( bool ) {

  var router_name_1, router_name_2 string

  for i := 0; i < 100; i ++ {
    
    router_name_1 = merc.random_router_name() 
    router_name_2 = merc.random_router_name() 

    if router_name_1 == router_name_2 {
      continue
    }

    // Direction doesn't matter. If these two already have
    // ac connection, I don't want them.
    if merc.network.Are_connected ( router_name_1, router_name_2 ) {
      continue
    }

    umi ( merc.verbose, "make_random_connection: connecting %s and %s", router_name_1, router_name_2 )
    merc.network.Connect_router ( router_name_1, router_name_2 )
    return true
  }

  return false
}





//=======================================================================
// Topology Commands.
//=======================================================================





func linear ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "linear" ]
  parse_command_line ( merc, cmd, command_line )

  count             := cmd.unlabelable_int.int_value
  requested_version := cmd.unlabelable_string.string_value

  // If no version supplied, use default.
  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  } 

  // Make the requested routers.
  var router_name string
  var temp_names [] string
  for i := 0; i < count; i ++ {
    router_name = get_next_interior_router_name ( merc )

    if requested_version == "RANDOM" {
      version = merc.random_version_name()
    }

    merc.network.Add_router ( router_name, 
                              version,
                              merc.session.config_path,
                              merc.session.log_path )
    temp_names = append ( temp_names, router_name )
    umi ( merc.verbose, "linear: added router |%s| with version |%s|.", router_name, version )
  }

  // And connect them.
  for index, name := range temp_names {
    if index < len(temp_names) - 1 {
      pitcher := name
      catcher := temp_names [ index + 1 ]
      merc.network.Connect_router ( pitcher, catcher )
      umi ( merc.verbose, "linear: connected router |%s| to router |%s|", pitcher, catcher )
    }
  }
}





func mesh ( merc  * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "mesh" ]
  parse_command_line ( merc, cmd, command_line )

  count             := cmd.unlabelable_int.int_value
  requested_version := cmd.unlabelable_string.string_value

  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  }

  // Make the requested routers.
  var router_name string
  var temp_names [] string
  for i := 0; i < count; i ++ {
    router_name = get_next_interior_router_name ( merc )
    if requested_version == "random" {
      version = merc.random_version_name()
    }
    merc.network.Add_router ( router_name, 
                              version,
                              merc.session.config_path,
                              merc.session.log_path )
    temp_names = append ( temp_names, router_name )
    umi ( merc.verbose, "mesh: added router |%s| with version |%s|.", router_name, version )
  }

  // And connect them.
    var catcher string
  for index, pitcher := range temp_names {
    if index < len(temp_names) - 1 {
      for j := index + 1; j < len(temp_names); j ++ {
        catcher = temp_names[j]
        merc.network.Connect_router ( pitcher, catcher )
        if merc.verbose {
          umi ( merc.verbose, "mesh: connected router |%s| to router |%s|", pitcher, catcher )
        }
      }
    }
  }
}





func ring ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "ring" ]
  parse_command_line ( merc, cmd, command_line )

  count             := cmd.unlabelable_int.int_value
  requested_version := cmd.unlabelable_string.string_value

  // If no version supplied, use default.
  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  } 

  // Make the requested routers.
  var router_name string
  var temp_names [] string
  for i := 0; i < count; i ++ {
    router_name = get_next_interior_router_name ( merc )

    if requested_version == "RANDOM" {
      version = merc.random_version_name()
    }

    merc.network.Add_router ( router_name, 
                              version,
                              merc.session.config_path,
                              merc.session.log_path )
    temp_names = append ( temp_names, router_name )
    umi ( merc.verbose, "ring: added router |%s| with version |%s|.", router_name, version )
  }

  // And connect them.
  // This part is just like in a linear network.
  var pitcher, catcher string
  for index, name := range temp_names {
    if index < len(temp_names) - 1 {
      pitcher = name
      catcher = temp_names [ index + 1 ]
      merc.network.Connect_router ( pitcher, catcher )
      umi ( merc.verbose, "ring: connected router |%s| to router |%s|", pitcher, catcher )
    }
  }

  // And now close the circle.
  pitcher = temp_names [ len(temp_names) - 1 ]
  catcher = temp_names [ 0 ]
  merc.network.Connect_router ( pitcher, catcher )
  umi ( merc.verbose, "ring: connected router |%s| to router |%s|", pitcher, catcher )
}





func star ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "star" ]
  parse_command_line ( merc, cmd, command_line )

  count             := cmd.unlabelable_int.int_value
  requested_version := cmd.unlabelable_string.string_value

  // If no version supplied, use default.
  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  } 

  ring_size := count - 1

  /*----------------------------------------------
    First Make a ring
  ----------------------------------------------*/
  // Make the requested routers.
  var router_name string
  var ring_names [] string
  for i := 0; i < ring_size; i ++ {
    router_name = get_next_interior_router_name ( merc )

    if requested_version == "RANDOM" {
      version = merc.random_version_name()
    }

    merc.network.Add_router ( router_name, 
                              version,
                              merc.session.config_path,
                              merc.session.log_path )
    ring_names = append ( ring_names, router_name )
    umi ( merc.verbose, "star: added router |%s| with version |%s|.", router_name, version )
  }

  // And connect them.
  // This part is just like in a linear network.
  var pitcher, catcher string
  for index, name := range ring_names {
    if index < len(ring_names) - 1 {
      pitcher = name
      catcher = ring_names [ index + 1 ]
      merc.network.Connect_router ( pitcher, catcher )
      umi ( merc.verbose, "ring: connected router |%s| to router |%s|", pitcher, catcher )
    }
  }

  // And now close the circle.
  pitcher = ring_names [ len(ring_names) - 1 ]
  catcher = ring_names [ 0 ]
  merc.network.Connect_router ( pitcher, catcher )
  umi ( merc.verbose, "ring: connected router |%s| to router |%s|", pitcher, catcher )


  /*----------------------------------------------
    Now add a center to the ring.
  ----------------------------------------------*/
  router_name = get_next_interior_router_name ( merc )
  if requested_version == "RANDOM" {
    version = merc.random_version_name()
  }
  merc.network.Add_router ( router_name,
                            version,
                            merc.session.config_path,
                            merc.session.log_path )
  umi ( merc.verbose, "star: added router |%s| with version |%s|.", router_name, version )

  // And connect the center to all other routers.
  pitcher = router_name
  for _, catcher := range ring_names {
    merc.network.Connect_router ( pitcher, catcher )
    umi ( merc.verbose, "ring: connected router |%s| to router |%s|", pitcher, catcher )
  }
}





func teds_diamond ( merc  * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "teds_diamond" ]
  parse_command_line ( merc, cmd, command_line )

  count             := 4
  requested_version := cmd.unlabelable_string.string_value

  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  }

  // Make the requested routers.
  var router_name string
  var temp_names [] string
  for i := 0; i < count; i ++ {
    router_name = get_next_interior_router_name ( merc )
    if requested_version == "random" {
      version = merc.random_version_name()
    }
    merc.network.Add_router ( router_name, 
                              version,
                              merc.session.config_path,
                              merc.session.log_path )
    temp_names = append ( temp_names, router_name )
    umi ( merc.verbose, "teds_diamond: added router |%s| with version |%s|.", router_name, version )
  }

  // And connect them.
    var catcher string
  for index, pitcher := range temp_names {
    if index < len(temp_names) - 1 {
      for j := index + 1; j < len(temp_names); j ++ {
        catcher = temp_names[j]
        merc.network.Connect_router ( pitcher, catcher )
        if merc.verbose {
          umi ( merc.verbose, "teds_diamond: connected router |%s| to router |%s|", pitcher, catcher )
        }
      }
    }
  }

  // Now make the two outliers.
  outlier := get_next_interior_router_name ( merc )
  if requested_version == "random" {
    version = merc.random_version_name()
  }
  merc.network.Add_router ( outlier, 
                            version,
                            merc.session.config_path,
                            merc.session.log_path )
  umi ( merc.verbose, "teds_diamond: added router |%s| with version |%s|.", outlier, version )
  catcher = temp_names[0]
  merc.network.Connect_router ( outlier, catcher )
  umi ( merc.verbose, "teds_diamond: connected router |%s| to router |%s|", outlier, catcher )
  catcher = temp_names[1]
  merc.network.Connect_router ( outlier, catcher )
  umi ( merc.verbose, "teds_diamond: connected router |%s| to router |%s|", outlier, catcher )


  outlier = get_next_interior_router_name ( merc )
  if requested_version == "random" {
    version = merc.random_version_name()
  }
  merc.network.Add_router ( outlier, 
                            version,
                            merc.session.config_path,
                            merc.session.log_path )
  umi ( merc.verbose, "teds_diamond: added router |%s| with version |%s|.", outlier, version )
  catcher = temp_names[2]
  merc.network.Connect_router ( outlier, catcher )
  umi ( merc.verbose, "teds_diamond: connected router |%s| to router |%s|", outlier, catcher )
  catcher = temp_names[3]
  merc.network.Connect_router ( outlier, catcher )
  umi ( merc.verbose, "teds_diamond: connected router |%s| to router |%s|", outlier, catcher )
}





func random_network ( merc  * Merc, command_line * lisp.List, _ string ) {

  if len(merc.network.Versions) < 1 {
    ume ( "routers: You must define at least one version before creating routers." )
    return
  }

  cmd := merc.commands [ "random_network" ]
  parse_command_line ( merc, cmd, command_line )

  n_routers         := cmd.unlabelable_int.int_value
  requested_version := cmd.unlabelable_string.string_value

  // If no version name was supplied, use default.
  var version string
  if requested_version == "" {
    version = merc.network.Default_version.Name
  }

  // Make the requested routers.
  var router_name string
  for i := 0; i < n_routers; i ++ {
    router_name = get_next_interior_router_name ( merc )

    if requested_version == "RANDOM" {
      version = merc.random_version_name()
    }

    merc.network.Add_router ( router_name, 
                              version, 
                              merc.session.config_path,
                              merc.session.log_path )
    umi ( merc.verbose, 
          "routers: added router |%s| with version |%s|.", 
          router_name, 
          version )
  }

  n_connections := 0
  for {
    if merc.network.Is_the_network_connected ( ) {
      umi ( merc.verbose, 
            "random_network : made network with %d routers and %d connections.", 
            n_routers,
            n_connections )
      return
    }

    if ! merc.make_random_connection ( ) {
      ume ( "random_network : make_random_connection() failed. Can't make connected network!" )
      return
    }

    n_connections ++
  }

}





//=======================================================================
// End Topology Commands.
//=======================================================================





func run ( merc  * Merc, command_line * lisp.List, _ string ) {
  merc.network.Init ( )
  merc.network.Run  ( )

  merc.network_running = true
  umi ( merc.verbose, "run: network is running." )
}





func quit ( merc * Merc, command_line * lisp.List, _ string ) {
  if merc.network_running {
    merc.network.Halt ( )
  }
  umi ( merc.verbose, "Mercury quitting." )
  merc.mercury_log_file.Close()
  os.Exit ( 0 )
}





func console_ports ( merc * Merc, command_line * lisp.List, _ string ) {
  merc.network.Print_console_ports ( )
}





func is_a_command_name ( merc * Merc, name string ) (bool) {
  for _, cmd := range ( merc.commands ) {
    if name == cmd.name {
      return true
    }
  }
  return false
}





func help_for_command ( merc * Merc, cmd * command ) {
  fp ( os.Stdout, "\n    %s : %s\n", cmd.name, cmd.help )

  longest_arg_name := 0
  var temp_names [] string
  for _, arg := range cmd.argmap {
    temp_names = append ( temp_names, arg.name )
    if len(arg.name) > longest_arg_name {
      longest_arg_name = len(arg.name)
    }
  }

  sort.Strings ( temp_names )

  for _, arg_name := range temp_names {
    pad_size := longest_arg_name - len(arg_name)
    pad      := strings.Repeat(" ", pad_size)
    arg      := cmd.argmap [ arg_name ]

    str := fmt.Sprintf ( "      %s%s : ", arg_name, pad )
    if arg.unlabelable {
      str = fmt.Sprintf ( "%sUNLABELABLE ", str )
    }
    if arg.help != "" {
      str = fmt.Sprintf ( "%s%s", str, arg.help )
    }
    if arg.default_value != "" {
      str = fmt.Sprintf ( "%s -- default: |%s|", str, arg.default_value )
    }
    fp ( os.Stdout, "%s\n", str )
  }
  fp ( os.Stdout, "\n\n" )
}





func kill ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "kill" ]
  parse_command_line ( merc, cmd, command_line )

  router_name := cmd.unlabelable_string.string_value

  if err := merc.network.Halt_router ( router_name ); err != nil {
    ume ( "kill: no such router |%s|", router_name )
    return
  }

  umi ( merc.verbose, "kill: killing router |%s|", router_name )
}





func kill_and_restart ( merc * Merc, command_line * lisp.List, _ string ) {

  if ! merc.network.Running {
    ume ( "kill_and_restart: the network is not running." )
    return
  }

  cmd := merc.commands [ "kill_and_restart" ]
  parse_command_line ( merc, cmd, command_line )

  router_name := cmd.unlabelable_string.string_value
  pause       := cmd.unlabelable_int.int_value

  if err := merc.network.Halt_and_restart_router ( router_name, pause ); err != nil {
    ume ( "kill_and_restart: no such router |%s|", router_name )
    return
  }
}





func help ( merc * Merc, command_line * lisp.List, _ string ) {
  // Get a sorted list of command names, 
  // and find the longest one.
  longest_command_name := 0
  cmd_names := make ( []string, 0 )
  for _, cmd := range merc.commands {
    cmd_names = append ( cmd_names, cmd.name )
    if len(cmd.name) > longest_command_name {
      longest_command_name = len(cmd.name)
    }
  }
  sort.Strings ( cmd_names )

  // If there is an arg on the command line, the 
  // user is asking for help with a specific command
  if command_line != nil && len(command_line.Elements) > 1 {
    requested_command, _ := command_line.Get_atom ( 1 )
    if is_a_command_name ( merc, requested_command ) {
      cmd := merc.commands [ requested_command ]
      help_for_command ( merc, cmd )
    } else {
      // The user did not enter a command name.
      // Maybe it is the first few letters of a command?
      // Give him the first one that matches.
      for _, cmd_name := range cmd_names {
        if strings.HasPrefix ( cmd_name, requested_command ) {
          cmd := merc.commands [ cmd_name ]
          help_for_command ( merc, cmd ) 
        }
      }
    }
  } else {
    // No arg on command line. The user 
    // wants all commands. Get the names.
    for _, name := range cmd_names {
      cmd      := merc.commands [ name ]
      pad_size := longest_command_name - len(name)
      pad      := strings.Repeat(" ", pad_size)
      fp ( os.Stdout, "    %s%s : %s\n", name, pad, cmd.help )
    }
  }
}





func sleep ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "sleep" ]
  parse_command_line ( merc, cmd, command_line )

  nap_time := cmd.unlabelable_int.int_value
  umi ( merc.verbose, "sleep: sleeping for %d seconds.", nap_time )
  time.Sleep ( time.Duration(nap_time) * time.Second )
}





func start_client_status_check ( merc * Merc, command_line * lisp.List, _ string ) {
  if ! merc.network.Running {
    ume ( "start_client_status_check: the network is not running." )
    return
  }

  cmd := merc.commands [ "start_client_status_check" ]
  parse_command_line ( merc, cmd, command_line )

  seconds := cmd.unlabelable_int.int_value
  umi ( merc.verbose, "start_client_status_check: every %d seconds.", seconds )
  merc.network.Start_client_status_check ( seconds )
}





func failsafe ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "failsafe" ]
  parse_command_line ( merc, cmd, command_line )

  seconds := cmd.unlabelable_int.int_value
  umi ( merc.verbose, "failsafe time set to %d seconds after client status checking begins.\n", seconds )
  merc.network.Failsafe = seconds
}





func wait_for_network ( merc * Merc, command_line * lisp.List, _ string ) {
  cmd := merc.commands [ "wait_for_network" ]
  parse_command_line ( merc, cmd, command_line )

  umi ( merc.verbose, "wait_for_network: waiting for network to stabilize." )

  router_names := merc.network.Get_interior_routers_names ( )
  paths        := merc.network.Get_router_log_file_paths ( router_names )

  var total_size, previous_size int64

  previous_size = 0

  for i := 0; i < 10; i ++ {
    total_size = 0
    for _, file_path := range paths {
      file_info, err := os.Stat ( file_path )
      if err == nil {
        total_size += file_info.Size()
      }
    }

    if total_size > 0 {
      if total_size == previous_size {
        umi ( merc.verbose, "wait_for_network: network has stabilized." )
        return
      }
      umi ( merc.verbose, "wait_for_network: router log files still active: size %d", total_size )
    }

    time.Sleep ( 2 * time.Second )
    previous_size = total_size
  }
}





func reset ( merc * Merc, command_line * lisp.List, _ string ) {
  merc.verbose         = false
  merc.echo            = false
  merc.prompt          = false
  merc.network_running = false
  merc.receiver_count  = 0
  merc.sender_count    = 0
  merc.edge_count      = 0
  merc.versions        = nil
  merc.default_version = nil

  merc.network.Reset ( )
}





func example_test_1 ( merc * Merc, command_line * lisp.List, _ string ) {
  // cmd := merc.commands [ "example_test_1" ]
  var command_lines [] string

  command_lines = append ( command_lines, "seed PID" )
  command_lines = append ( command_lines, "verbose" )
  command_lines = append ( command_lines, "version_roots name latest dispatch /home/mick/latest/install/dispatch proton /home/mick/latest/install/proton" )
  command_lines = append ( command_lines, "routers 1" )
  command_lines = append ( command_lines, "send 1 A" )
  command_lines = append ( command_lines, "recv A 1" )
  command_lines = append ( command_lines, "run"      )
  command_lines = append ( command_lines, "sleep 60" )
  command_lines = append ( command_lines, "reset" )

  // utils.Set_Top_Freq ( merc.cpu_freqs )

  n_tests := 5
  for test_number := 1; test_number <= n_tests; test_number ++ {
    merc.network.Set_results_path ( merc.session.name + fmt.Sprintf ( "/results/iteration_%.3d", test_number ) )

    fp ( os.Stdout, "===================================================\n" )
    fp ( os.Stdout, "                    Test %d                        \n", test_number )
    fp ( os.Stdout, "===================================================\n" )

    for _, line := range command_lines {
      process_line ( merc, line )
    }
  }
}





func latency_test_1 ( merc * Merc, command_line * lisp.List, _ string ) {
  // cmd := merc.commands [ "example_test_1" ]
  var command_lines [] string

  command_lines = append ( command_lines, "seed PID" )
  command_lines = append ( command_lines, "verbose" )
  command_lines = append ( command_lines, "version_roots name latest dispatch /home/mick/latest/install/dispatch proton /home/mick/latest/install/proton" )
  command_lines = append ( command_lines, "routers 1" )
  //command_lines = append ( command_lines, "recv A 100" )
  command_lines = append ( command_lines, "run"      )
  //command_lines = append ( command_lines, "sleep 10" )
  //command_lines = append ( command_lines, "send 100 A" )
  //command_lines = append ( command_lines, "run"      )
  //command_lines = append ( command_lines, "sleep 60" )
  //command_lines = append ( command_lines, "reset" )

  n_tests := 1
  for test_number := 1; test_number <= n_tests; test_number ++ {
    merc.network.Set_results_path ( merc.session.name + fmt.Sprintf ( "/results/iteration_%.3d", test_number ) )

    fp ( os.Stdout, "===================================================\n" )
    fp ( os.Stdout, "                    Test %d                        \n", test_number )
    fp ( os.Stdout, "===================================================\n" )

    for _, line := range command_lines {
      process_line ( merc, line )
    }


    // At this point the network should be running.
    first_router_name := merc.network.First_router_name ( )
    last_router_name  := merc.network.Last_router_name  ( )
    fmt.Fprintf ( os.Stdout, "first |%s|   last |%s|\n", first_router_name, last_router_name )
  }
}






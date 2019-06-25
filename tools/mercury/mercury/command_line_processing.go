package main

import (
  "bufio"
  "fmt"
  "os"
  "strings"
  "strconv"

  "lisp"
)



var mercury = '\u263F'




func read_file ( merc * Merc, file_name string ) {

  file, err := os.Open ( file_name )
  if err != nil {
    panic ( err )
  }
  defer file.Close()

  scanner := bufio.NewScanner ( file )

  for scanner.Scan() {
    process_line ( merc, scanner.Text() )
  }

  if err := scanner.Err(); err != nil {
    panic ( err )
  }
}





// Process command lines that are coming in either from
// a file or from the command line.
func process_line ( merc * Merc, line string ) {

  first_nonwhitespace := merc.first_nonwhitespace_rgx.FindString ( line )
  if first_nonwhitespace == "" {
    // If the line is just empty, don't do anything with it.
    return
  }

  if merc.echo {
    fp ( os.Stdout, "%c echo: %s\n", mercury, line )
  }

  // Line preprocessing.
  if strings.Contains ( line, "PID" ) {
    pid_str := strconv.Itoa(os.Getpid())
    line = strings.Replace ( line, "PID", pid_str, -1 )
  }

  fmt.Fprintf ( merc.mercury_log_file, "%s\n", line )

  if first_nonwhitespace == "#" {
    // This line is a comment.
    return
  }

  // Clean up the line
  line = strings.Replace ( line, "\n", "", -1 )
  line = merc.line_rgx.ReplaceAllString ( line, " " )
  fields := lisp.Listify ( line )
  _, list := lisp.Parse_from_string ( fields )

  call_command ( merc, list, line )

  if merc.prompt {
    prompt_reader := bufio.NewReader ( os.Stdin )
    fp ( os.Stdout, "%c: hit enter to continue.\n", mercury )
    prompt_reader.ReadString ( '\n' )
  }
}





// This gets called by the individual commands, if they 
// want standardized command-line processing. Some don't.
func parse_command_line ( merc *      Merc, 
                          cmd          * command, 
                          command_line * lisp.List ) {

  var err error

  // Fill in all args with their default values.
  // First the unlabelables
  if cmd.unlabelable_int != nil {
    cmd.unlabelable_int.int_value, _ = strconv.Atoi(cmd.unlabelable_int.default_value)
  }
  if cmd.unlabelable_string != nil {
    cmd.unlabelable_string.string_value = cmd.unlabelable_string.default_value
  }

  // And now all the labeled args.
  for _, arg := range cmd.argmap {
    if arg.data_type == "string " {
      arg.string_value = arg.default_value
    } else {
      arg.int_value, _ = strconv.Atoi ( arg.default_value )
    }
  }

  // Process the command line.
  // Get all the labeled args from the command line.
  // They and their values are removed as they are parsed.
  // If there are any unlabeled args, they will be left over after 
  // these are removed.
  for _, arg := range cmd.argmap {

    if arg.data_type != "list" {
      str_val := command_line.Get_atom_value_and_remove ( arg.name )
      if str_val != "" {
        // The user provided a value.
        if arg.data_type == "string" {
          arg.string_value = str_val
          arg.explicit = true
        } else {
          arg.int_value, err = strconv.Atoi ( str_val )
          if err != nil {
            ume ( "parse_command_line: error reading int from |%s|", str_val )
            return
          }
          arg.explicit = true
        }
      } else {
        // This arg was not on the command line.
        // Give it its default value. If it has one.
        arg.explicit = false
        arg.string_value = arg.default_value
        if arg.data_type == "int" && arg.default_value != "" {
          val, err := strconv.Atoi ( arg.default_value )
          if err != nil {
            ume ( "parse_command_line: error converting default val |%s| of arg |%s| to int.", 
                          arg.default_value,
                          arg.name )
          } else {
            arg.int_value = val
          }
        }
      }
    } else if arg.data_type == "list" {
      arg.list_value = command_line.Get_list_value_and_remove ( arg.name )
    }
  }

  // If this command has unlabelable args, get them last.
  // Get the unlabelable string.
  if cmd.unlabelable_string != nil {
    ul_str, e2 := command_line.Get_string_cdr ( )
    if e2 == nil {
      // Fill in the value, so the command can get at it.
      cmd.unlabelable_string.string_value = ul_str
      cmd.unlabelable_string.explicit     = true
    }
  }

  // Get the unlabelable int.
  if cmd.unlabelable_int != nil {
    name := cmd.unlabelable_int.name
    ul_str, e2 := command_line.Get_int_cdr ( ) 
    if e2 == nil {
      var err error
      cmd.unlabelable_int.int_value, err = strconv.Atoi ( ul_str )
      if err != nil {
        ume ( "parse_command_line: error reading value for |%s| : |%s|", 
              name, 
              err.Error() )
        cmd.unlabelable_int.explicit = false
      } else {
        cmd.unlabelable_int.explicit = true
      }
    }
  }
}







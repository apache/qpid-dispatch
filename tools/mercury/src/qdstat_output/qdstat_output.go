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

package qdstat_output

import ( 
         "errors"
         "fmt"
         "os"
         "strings"
         "strconv"
       )


var fp          = fmt.Fprintf
var module_name = "router"



type Line struct {
  name       string
  size       int
  batch      int
  in_threads int
}



type Qdstat_output struct {
  Lines [] * Line
}





func New_line ( name string,
                  size int,
                  batch int,
                  in_threads int ) ( * Line ) {
  var f * Line
  f = & Line { name       : name,
               size       : size,
               batch      : batch,
               in_threads : in_threads }
  return f
}





func ( l * Line ) Print ( f * os.File ) {
  fp ( f, "struct" )
  fp ( f, "{\n" )
  fp ( f, "  name       : %s\n", l.name )
  fp ( f, "  size       : %d\n", l.size )
  fp ( f, "  batch      : %d\n", l.batch )
  fp ( f, "  in_threads : %d\n", l.in_threads )
  fp ( f, "}\n" )
}





func ( o * Qdstat_output ) Print ( f * os.File ) {
  for _, s := range o.Lines {
    s.Print ( f )
  }
}





func ( o * Qdstat_output ) Print_nonzero ( f * os.File ) {
  for _, l := range o.Lines {
    if l.in_threads != 0 {
      l.Print ( f )
    }
  }
}



func ( this_line * Line ) Diff ( that_line * Line ) ( * Line ) {
  if this_line.name != that_line.name {
    panic ( errors.New ( "lines differ." ) )
  }
  
  new_line := * this_line
  new_line.in_threads = this_line.in_threads - that_line.in_threads

  return & new_line
}





func New_qdstat_output ( raw_output string ) ( * Qdstat_output ) {

  var result * Qdstat_output

  result = & Qdstat_output {} 
  lines := strings.Split ( string(raw_output), "\n" )
  for _, line := range lines {
    words := strings.Fields ( line )
    if len(words) <= 0 {
     continue
    }
    if strings.HasPrefix ( words[0], "qd" ) {

      name          := words[0]
      size, _       := strconv.Atoi( strings.Replace(words[1], ",", "", -1) )
      batch, _      := strconv.Atoi( strings.Replace(words[2], ",", "", -1) )
      in_threads, _ := strconv.Atoi( strings.Replace(words[5], ",", "", -1) )

      result.Lines = append ( result.Lines, New_line ( name, size, batch, in_threads ) )
    }
  }

  return result
}





func ( r1 * Qdstat_output ) Find_line ( target_name string ) ( * Line ) {
  for _, l := range r1.Lines {
    if l.name == target_name {
      return l
    }
  }

  return nil
}





func ( r1 * Qdstat_output ) Diff ( r2 * Qdstat_output ) ( * Qdstat_output ) {

  result := & Qdstat_output {}

  for _, l1 := range r1.Lines {
    l2 := r2.Find_line ( l1.name )
    if l2 != nil {
      new_line := l1.Diff ( l2 )
      result.Lines = append ( result.Lines, new_line )
    }
  }

  return result
}






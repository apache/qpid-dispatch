
/*
  Saint Greenspun's Tenth Law:
  "Any sufficiently complicated C or Fortran program contains an ad-hoc, 
  informally-specified, bug-ridden, slow implementation of half of CommonLisp."

  Mick's 2019 Addendum: "Or Go program."
*/


package lisp


import ( "errors"
         "fmt"
         "os"
         "strconv"
         "strings"
       )


var fp = fmt.Fprintf



type Atom string



type Element struct {
  atom    Atom
  list  * List
}



type List struct {
  Elements [] * Element
}





func New_list ( ) ( * List ) {
  l := & List { }
  return l
}





func new_element_atom ( atom Atom ) ( * Element ) {
  return & Element { atom : atom,
                     list : nil }
}





func new_element_list ( l * List ) ( * Element ) {
  return & Element { atom : "",
                     list : l }
}





func ( l * List ) Append_atom ( a Atom ) {
  l.Elements = append ( l.Elements, new_element_atom(a) )
}





func ( l * List ) Append_list ( l2 * List ) {
  l.Elements = append ( l.Elements, new_element_list(l2) )
}





func ( l * List ) Get_atom ( index int ) (string, error) {

  if index >= len ( l.Elements) {
    return "", errors.New ( "Index out of range." )
  }

  return string ( l.Elements[index].atom ), nil
}





func ( l * List ) Get_string ( ) (string, error) {
  for _, el := range l.Elements {
    if el.atom != "" {
      // I do not want integers here.
      if _, err := strconv.Atoi ( string(el.atom) ); err != nil {
        return string(el.atom), nil
      }
    }
  }

  return "", errors.New ( "No string atom in list." )
}





func ( l * List ) Get_string_cdr ( ) (string, error) {
  for i, el := range l.Elements {
    if i == 0 {
      continue // ignore first element
    }
    if el.atom != "" {
      // I do not want integers here.
      if _, err := strconv.Atoi ( string(el.atom) ); err != nil {
        return string(el.atom), nil
      }
    }
  }

  return "", errors.New ( "No string atom in list." )
}





func ( l * List ) Get_int_cdr ( ) (string, error) {
  for i, el := range l.Elements {
    if i == 0 {
      continue // ignore first element
    }
    if el.atom != "" {
      // I do want integers here.
      if _, err := strconv.Atoi ( string(el.atom) ); err == nil {
        return string(el.atom), nil
      }
    }
  }

  return "", errors.New ( "No string atom in list." )
}





func ( l * List ) Get_strings ( ) ( []string ) {

  var strings [] string

  for _, el := range l.Elements {
    if el.atom != "" {
      // I do not want integers here.
      if _, err := strconv.Atoi ( string(el.atom) ); err != nil {
        strings = append ( strings, string(el.atom) )
      }
    }
  }

  return strings
}





func ( l * List ) Get_int ( ) (int, error) {

  var value int
  var err   error

  for _, el := range l.Elements {
    if el.atom != "" {
      value, err = strconv.Atoi ( string(el.atom) )
      if err == nil {
        return value, nil
      }
    }
  }

  return 0, errors.New ( "No int atom in list." )
}





func ( l * List ) Match_atom ( pattern string ) ( string ) {
  for _, el := range l.Elements {
    if el.atom != "" {
      if strings.Contains ( string(el.atom), pattern ) {
        return string ( el.atom )
      }
    }
  }

  return ""
}





func ( l * List ) Get_value ( attr string ) ( string ) {
  for index, el := range l.Elements {
    if string(el.atom) == attr {
      value_element := l.Elements [ index + 1 ]
      return string ( value_element.atom )
    }
  }

  return ""
}





func ( l * List ) Get_atom_value_and_remove ( attr string ) ( string ) {
  for index, el := range l.Elements {
    if string(el.atom) == attr {
      // Save the value to be returned.
      value_element := l.Elements [ index + 1 ]
      value         := string ( value_element.atom )
      // Remove the value from the list.
      l.Elements = append ( l.Elements [ : index ], l.Elements [ index+2 : ] ...)
      return value
    }
  }

  return ""
}




func ( l * List ) Get_list_value_and_remove ( attr string ) ( * List ) {
  for index, el := range l.Elements {
    if string(el.atom) == attr {
      // Save the value to be returned.
      value_element := l.Elements [ index + 1 ]
      value         := value_element.list 
      // Remove the value from the list.
      l.Elements = append ( l.Elements [ : index ], l.Elements [ index+2 : ] ...)
      return value
    }
  }

  return nil
}





func ( l * List ) Print ( indent int ) {
  indent_str := strings.Repeat ( " ", indent )
  for _, el := range ( l.Elements ) {
    if el.atom != "" {
      fp ( os.Stdout, "%s%s\n", indent_str, el.atom )
    } else if el.list != nil {
      el.list.Print ( indent + 2 )
    }
  }
}





func ( l * List ) Length ( ) ( int ) {
  return len ( l.Elements )
}





func Parse_from_string ( fields [] string ) ( int, * List ) {

  if fields[0] != "(" {
    fp ( os.Stdout, 
         "Parse_from_string error: bad list start: |%s|\n", 
         fields[0] )
    return 0, nil
  }

  list := New_list ( )

  index := 1

  for {
    // This is the end of a list.
    if fields[index] == ")" {
      return index, list
    }

    // The beginning of a new list.
    // Parse it, add it to my list, 
    // and skip its fields.
    if fields[index] == "(" {
      used_fields, sublist := Parse_from_string ( fields [ index: ] )
      list.Append_list ( sublist )
      index += (used_fields + 1)
      continue
    }
    
    // Just a regular atom. Add it and keep going.
    list.Append_atom ( Atom(fields[index]) )
    index ++
  }

  return index, list
}





func Listify ( str string ) ( [] string ) {
  var listification [] string
  listification = append ( listification, "(" )
  fields := strings.Fields(str)
  listification = append ( listification, fields... )
  listification = append ( listification, ")" )
  return listification
}








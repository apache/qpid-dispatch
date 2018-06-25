/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/
export let posint = function() {
  return {
    require: 'ngModel',

    link: function(scope, elem, attr, ctrl) {
      // input type number allows + and - but we don't want them so filter them out
      elem.bind('keypress', function(event) {
        let nkey = !event.charCode ? event.which : event.charCode;
        let skey = String.fromCharCode(nkey);
        let nono = '-+.,';
        if (nono.indexOf(skey) >= 0) {
          event.preventDefault();
          return false;
        }
        // firefox doesn't filter out non-numeric input. it just sets the ctrl to invalid
        if (/[!@#$%^&*()]/.test(skey) && event.shiftKey || // prevent shift numbers
          !( // prevent all but the following
            nkey <= 0 || // arrows
            nkey == 8 || // delete|backspace
            nkey == 13 || // enter
            (nkey >= 37 && nkey <= 40) || // arrows
            event.ctrlKey || event.altKey || // ctrl-v, etc.
            /[0-9]/.test(skey)) // numbers
        ) {
          event.preventDefault();
          return false;
        }
      });
      // check the current value of input
      var _isPortInvalid = function(value) {
        let port = value + '';
        let isErrRange = false;
        // empty string is valid
        if (port.length !== 0) {
          let n = ~~Number(port);
          if (n < 1 || n > 65535) {
            isErrRange = true;
          }
        }
        ctrl.$setValidity('range', !isErrRange);
        return isErrRange;
      };

      //For DOM -> model validation
      ctrl.$parsers.unshift(function(value) {
        return _isPortInvalid(value) ? undefined : value;
      });

      //For model -> DOM validation
      ctrl.$formatters.unshift(function(value) {
        _isPortInvalid(value);
        return value;
      });
    }
  };
};


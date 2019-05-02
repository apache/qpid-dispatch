#!/usr/bin/env python

#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

from collections import defaultdict
import common

class ShortNames():
    '''
    Name shortener.
    The short name for display is "name_" + index(longName)
    Embellish the display name with an html popup
    Link and endpoint names, and data are tracked separately
    Names longer than threshold are shortened
    Each class has a prefix used when the table is dumped as HTML
    '''
    def __init__(self, prefixText, _threshold=25):
        self.longnames = []
        self.prefix = prefixText
        self.threshold = _threshold
        self.customer_dict = defaultdict(list)

    def translate(self, lname, show_popup=False, customer=None):
        '''
        Translate a long name into a short name, maybe.
        Memorize all names, translated or not
        Strip leading/trailing double quotes
        :param lname: the name
        :return: If shortened HTML string of shortened name with popup containing long name else
        not-so-long name.
        '''
        if lname.startswith("\"") and lname.endswith("\""):
            lname = lname[1:-1]
        try:
            idx = self.longnames.index(lname)
        except:
            self.longnames.append(lname)
            idx = self.longnames.index(lname)
        # return as-given if short enough
        if customer is not None:
            self.customer_dict[lname].append(customer)
        if len(lname) < self.threshold:
            return lname
        sname = self.prefix + "_" + str(idx)
        if customer is not None:
            self.customer_dict[sname].append(customer)
        if show_popup:
            return "<span title=\"" + common.html_escape(lname) + "\">" + sname + "</span>"
        else:
            return sname

    def len(self):
        return len(self.longnames)

    def prefix(self):
        return self.prefix

    def shortname(self, idx):
        name = self.longnames[idx]
        if len(name) < self.threshold:
            return name
        return self.prefix + "_" + str(idx)

    def prefixname(self, idx):
        return self.prefix + "_" + str(idx)

    def sname_to_popup(self, sname):
        if not sname.startswith(self.prefix):
            raise ValueError("Short name '%s' does not start with prefix '%s'" % (sname, self.prefix))
        try:
            lname = self.longnames[ int(sname[ (len(self.prefix) + 1): ])]
        except:
            raise ValueError("Short name '%s' did not translate to a long name" % (sname))
        return "<span title=\"" + common.html_escape(lname) + sname + "</span>"

    def longname(self, idx, html_escape=False):
        '''
        Get the common.html_escape'd long name
        :param idx:
        :param html_escape: true if caller wants the string for html display
        :return:
        '''
        return common.html_escape(self.longnames[idx]) if html_escape else self.longnames[idx]

    def htmlDump(self, with_link=False):
        '''
        Print the name table as an unnumbered list to stdout
        long names are common.html_escape'd
        :param with_link: true if link name link name is hyperlinked targeting itself
        :return: null
        '''
        if len(self.longnames) > 0:
            print ("<h3>" + self.prefix + " Name Index</h3>")
            print ("<ul>")
            for i in range(0, len(self.longnames)):
                name = self.prefix + "_" + str(i)
                dump_anchor = "<a name=\"%s_dump\"></a>" % (name)
                if with_link:
                    name = "<a href=\"#%s\">%s</a>" % (name, name)
                print ("<li> " + dump_anchor + name + " - " + common.html_escape(self.longnames[i]) + "</li>")
            print ("</ul>")

    def sort_customers(self):
        for c in common.dict_iterkeys(self.customer_dict):
            l = self.customer_dict[c]
            self.customer_dict[c] = sorted(l, key=lambda lfl: lfl.datetime)

    def customers(self, sname):
        return self.customer_dict[sname]

    def sorted_indexes(self):
        return [self.longnames.index(sln) for sln in sorted(self.longnames)]

class Shorteners():
    def __init__(self):
        self.short_link_names = ShortNames("link", 15)
        self.short_addr_names = ShortNames("address")
        self.short_data_names = ShortNames("transfer", 2)
        self.short_peer_names = ShortNames("peer")
        self.short_rtr_names  = ShortNames("router")


if __name__ == "__main__":
    pass

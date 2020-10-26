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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

from time import strftime, gmtime
from qpid_dispatch_internal.compat import UNICODE


def YN(val):
  if val:
    return 'Y'
  return 'N'

def Commas(value):
  sval = str(value)
  result = ""
  while True:
    if len(sval) == 0:
      return result
    left = sval[:-3]
    right = sval[-3:]
    result = right + result
    if len(left) > 0:
      result = ',' + result
    sval = left

def TimeLong(value):
  day = value // (24 * 3600)
  time = value % (24 * 3600)
  hour = time // 3600
  time %= 3600
  minutes = time // 60
  time %= 60
  seconds = time
  return "%03d:%02d:%02d:%02d" % (day, hour, minutes, seconds)

def TimeShort(value):
  return strftime("%X", gmtime(value / 1000000000))

def NumKMG(value, base=1000):
    """
    Format large numbers in a human readable summary
    """
    # IEEE 1541 numeric suffix definitions:
    SUFFIX = {1024: ('KiB', 'MiB', 'GiB', 'TiB', 'PiB'),
              1000: ('k', 'm', 'g', 't', 'p')}

    def _numCell(fp, suffix):
        # adjust the precision based on the size
        if fp < 10.0:
            return "%.2f %s" % (fp, suffix)
        if fp < 100.0:
            return "%.1f %s" % (fp, suffix)
        return "%.0f %s" % (fp, suffix)

    if value < base:
        return "%d" % value

    # round down to a power of base:
    sx = SUFFIX[base]
    for i in range(len(sx)):
        value /= float(base);
        if value < base:
            return _numCell(value, sx[i])
    return _numCell(value, sx[-1])


class Header:
  """ """
  NONE = 1
  KMG = 2    # 1000 based units
  YN = 3
  Y = 4
  TIME_LONG = 5
  TIME_SHORT = 6
  DURATION = 7
  COMMAS = 8
  # This is a plain number, no formatting
  PLAIN_NUM = 9
  KiMiGi = 10  # 1024 based units

  def __init__(self, text, format=NONE):
    self.text = text
    self.format = format

  def __repr__(self):
    return self.text

  def __str__(self):
    return self.text

  def formatted(self, value):
    try:
      if value == None:
        return ''
      if self.format == Header.NONE:
        return value
      if self.format == Header.PLAIN_NUM:
        return PlainNum(value)
      if self.format == Header.KMG:
        return NumKMG(value)
      if self.format == Header.KiMiGi:
        return NumKMG(value, base=1024)
      if self.format == Header.YN:
        if value:
          return 'Y'
        return 'N'
      if self.format == Header.Y:
        if value:
          return 'Y'
        return ''
      if self.format == Header.TIME_LONG:
         return TimeLong(value)
      if self.format == Header.TIME_SHORT:
         return TimeShort(value)
      if self.format == Header.DURATION:
        if value < 0:
          value = 0
        sec = value / 1000000000
        min = sec / 60
        hour = min / 60
        day = hour / 24
        result = ""
        if day > 0:
          result = "%dd " % day
        if hour > 0 or result != "":
          result += "%dh " % (hour % 24)
        if min > 0 or result != "":
          result += "%dm " % (min % 60)
        result += "%ds" % (sec % 60)
        return result
      if self.format == Header.COMMAS:
        return Commas(value)
    except:
      return "?"


def PlainNum(value):
  try:
    ret_val = "%d" % value
    return ret_val
  except:
    return "%s" % value


class BodyFormat:
  """
  Display body format chooses between:
   CLASSIC - original variable-width, unquoted, text delimited by white space
   CSV     - quoted text delimited by commas
  """
  CLASSIC = 1
  CSV = 2

class CSV_CONFIG:
  """ """
  SEPERATOR = u','
  STRING_QUOTE = u'"'

class Display:
  """ Display formatting """
  
  def __init__(self, spacing=2, prefix="    ", bodyFormat=BodyFormat.CLASSIC):
    self.tableSpacing    = spacing
    self.tablePrefix     = prefix
    self.timestampFormat = "%X"
    if bodyFormat == BodyFormat.CLASSIC:
      self.printTable = self.table
    elif bodyFormat == BodyFormat.CSV:
      self.printTable = self.tableCsv
    else:
      raise Exception("Table body format must be CLASSIC or CSV.")

  def formattedTable(self, title, heads, rows):
    fRows = []
    for row in rows:
      fRow = []
      col = 0
      for cell in row:
        fRow.append(heads[col].formatted(cell))
        col += 1
      fRows.append(fRow)
    headtext = []
    for head in heads:
      headtext.append(head.text)
    self.printTable(title, headtext, fRows)

  def table(self, title, heads, rows):
    """ Print a table with autosized columns """

    # Pad the rows to the number of heads
    for row in rows:
      diff = len(heads) - len(row)
      for idx in range(diff):
        row.append("")

    print("%s" % title)
    if len (rows) == 0:
      return
    colWidth = []
    col      = 0
    line     = self.tablePrefix
    for head in heads:
      width = len (head)
      for row in rows:
        text = UNICODE(row[col])
        cellWidth = len(text)
        if cellWidth > width:
          width = cellWidth
      colWidth.append (width + self.tableSpacing)
      line = line + head
      if col < len (heads) - 1:
        for i in range (colWidth[col] - len (head)):
          line = line + " "
      col = col + 1
    print(line)
    line = self.tablePrefix
    for width in colWidth:
      for i in range (width):
        line = line + "="
    print(line)

    for row in rows:
      line = self.tablePrefix
      col  = 0
      for width in colWidth:
        text = UNICODE(row[col])
        line = line + text
        if col < len (heads) - 1:
          for i in range (width - len(text)):
            line = line + " "
        col = col + 1
      print(line)

  def tableCsv(self, title, heads, rows):
    """
    Print a table with CSV format.
    """

    def csvEscape(text):
      """
      Given a unicode text field, return the quoted CSV format for it
      :param text: a header field or a table row field
      :return:
      """
      if len(text) == 0:
        return ""
      else:
        text = text.replace(CSV_CONFIG.STRING_QUOTE, CSV_CONFIG.STRING_QUOTE*2)
        return CSV_CONFIG.STRING_QUOTE + text + CSV_CONFIG.STRING_QUOTE

    print("%s" % title)
    if len (rows) == 0:
      return

    print(','.join([csvEscape(UNICODE(head)) for head in heads]))
    for row in rows:
      print(','.join([csvEscape(UNICODE(item)) for item in row]))

  def do_setTimeFormat (self, fmt):
    """ Select timestamp format """
    if fmt == "long":
      self.timestampFormat = "%c"
    elif fmt == "short":
      self.timestampFormat = "%X"

  def timestamp (self, nsec):
    """ Format a nanosecond-since-the-epoch timestamp for printing """
    return strftime (self.timestampFormat, gmtime (nsec / 1000000000))

  def duration(self, nsec):
    if nsec < 0:
      nsec = 0
    sec = nsec / 1000000000
    min = sec / 60
    hour = min / 60
    day = hour / 24
    result = ""
    if day > 0:
      result = "%dd " % day
    if hour > 0 or result != "":
      result += "%dh " % (hour % 24)
    if min > 0 or result != "":
      result += "%dm " % (min % 60)
    result += "%ds" % (sec % 60)
    return result

class Sortable(object):
  """ """
  def __init__(self, row, sortIndex):
    self.row = row
    self.sortIndex = sortIndex
    if sortIndex >= len(row):
      raise Exception("sort index exceeds row boundary")

  def __lt__(self, other):
    return self.row[self.sortIndex] < other.row[self.sortIndex]

  def getRow(self):
    return self.row

class Sorter:
  """ """
  def __init__(self, heads, rows, sortCol, limit=0, inc=True):
    col = 0
    for head in heads:
      if head.text == sortCol:
        break
      col += 1
    if col == len(heads):
      raise Exception("sortCol '%s', not found in headers" % sortCol)

    list = []
    for row in rows:
      list.append(Sortable(row, col))
    list.sort()
    if not inc:
      list.reverse()
    count = 0
    self.sorted = []
    for row in list:
      self.sorted.append(row.getRow())
      count += 1
      if count == limit:
        break

  def getSorted(self):
    return self.sorted

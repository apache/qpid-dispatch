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
import system_test
import os
from quart import Quart, request
try:
    from quart.static import send_file
except ImportError:
    from quart.helpers import send_file

try:
    from quart.exceptions import HTTPStatusException
except ImportError:
    from werkzeug.exceptions import InternalServerError as HTTPStatusException

import json
app = Quart(__name__)


class MyInfo(object):
    def __init__(self, fname, lname, id=None):
        self.fname = fname
        self.lname = lname
        self.id = id
        #self.hobby = None
        #self.style = None


my_info = MyInfo(fname="John", lname="Doe")


def image_file(name):
    return os.path.join(system_test.DIR, 'images', name)


@app.route("/myinfo/delete/<id>", methods=["DELETE"])
async def delete_myinfo(id):  # noqa
    my_info.id = id
    jsonStr = json.dumps(my_info.__dict__)
    return jsonStr


@app.route('/myinfo', methods=['GET', 'POST', 'PUT'])
async def create_myinfo():
    form = await request.form
    fname = form['fname']
    lname = form['lname']
    message = "Success! Your first name is %s, last name is %s" % (fname, lname)
    return message


def large_string(length):
    i = 0
    ret_string = ""
    while (i < length):
        ret_string += str(i) + ","
        i += 1
    return ret_string


@app.route('/')
async def index():
    return large_string(1000)


@app.route('/largeget', methods=['GET'])
async def largeget():
    return large_string(50000)


@app.route('/patch', methods=['PATCH'])
async def patch():
    data = await request.data
    return data

# Return a 500 error, "Service Unavailable"


@app.route('/test/500')
async def service_unavailable():
    raise HTTPStatusException()


@app.route('/images/balanced-routing.png', methods=['GET'])
async def get_png_images():
    img_file = image_file("balanced-routing.png")
    return await send_file(img_file, mimetype='image/png')


@app.route('/images/apache.jpg', methods=['GET'])
async def get_jpg_images():
    img_file = image_file("apache.jpg")
    return await send_file(img_file, mimetype='image/jpg')

#app.run(port=5000, certfile='cert.pem', keyfile='key.pem')
app.run(port=os.getenv('SERVER_LISTEN_PORT'))

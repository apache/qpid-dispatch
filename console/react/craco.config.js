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

const webpack = require('webpack');

module.exports = {
    webpack: {
        plugins: [
            // Buffer is undefined:
            // https://github.com/webpack/changelog-v5/issues/10
            new webpack.ProvidePlugin({
                process: 'process/browser',
                Buffer: ['buffer', 'Buffer'],
            }),
        ],
        configure: {
            /* Any webpack configuration options: https://webpack.js.org/configuration */
            resolve: {
                fallback: {
                    "buffer": require.resolve("buffer/"),
                    "os": require.resolve("os-browserify/browser"),
                    "path": require.resolve("path-browserify"),
                    "util": require.resolve("util/")
                }
            }
        }
    }
};

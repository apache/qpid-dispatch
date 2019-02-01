var license = `/*
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
`;

const gulp = require('gulp'),
  mocha = require('gulp-mocha'),
  gulpif = require('gulp-if'),
  rollup = require('rollup-stream'),
  source = require('vinyl-source-stream'),
  buffer = require('vinyl-buffer'),
  concat = require('gulp-concat'),
  uglify = require('gulp-uglify'),
  terser = require('gulp-terser'),
  babel = require('gulp-babel'),
  ngAnnotate = require('gulp-ng-annotate'),
  cleanCSS = require('gulp-clean-css'),
  del = require('del'),
  eslint = require('gulp-eslint'),
  maps = require('gulp-sourcemaps'),
  insert = require('gulp-insert'),
  rename = require('gulp-rename'),
  fs = require('fs'),
  tsc = require('gulp-typescript'),
  tslint = require('gulp-tslint'),
  through = require('through2');

// temp directory for converted typescript files
const built_ts = 'built_ts';

// fetch command line arguments
const arg = (argList => {
  let arg = {}, a, opt, thisOpt, curOpt;
  for (a = 0; a < argList.length; a++) {
    thisOpt = argList[a].trim();
    opt = thisOpt.replace(/^-+/, '');

    if (opt === thisOpt) {
      // argument value
      if (curOpt) arg[curOpt] = opt;
      curOpt = null;
    }
    else {
      // argument name
      curOpt = opt;
      arg[curOpt] = true;
    }
  }
  return arg;
})(process.argv);

var src = arg.src ? arg.src + '/' : '';
var production = (arg.target === 'production');

const paths = {
  typescript: {
    src: src + 'plugin/**/*.ts',
    dest: built_ts
  },
  styles: {
    src: src + 'plugin/css/**/*.css',
    dest: 'dist/css/'
  },
  scripts: {
    src: [src + 'plugin/js/**/*.js'],
    dest: 'dist/js/'
  }
};
var touch = through.obj(function (file, enc, done) {
  var now = new Date;
  fs.utimes(file.path, now, now, done);
});

var clean = function () {
  return del(['dist', built_ts]);
};

var cleanup = function () {
  return del([built_ts]);
};

var styles = function () {
  return gulp.src(paths.styles.src)
    .pipe(maps.init())
    .pipe(cleanCSS())
    .pipe(rename({
      basename: 'dispatch',
      suffix: '.min'
    }))
    .pipe(insert.prepend(license))
    .pipe(maps.write('./'))
    .pipe(gulp.dest(paths.styles.dest));
};

var vendor_styles = function () {
  var vendor_lines = fs.readFileSync('vendor-css.txt').toString().split('\n');
  var vendor_files = vendor_lines.filter(function (line) {
    return (!line.startsWith('-') && line.length > 0);
  });
  return gulp.src(vendor_files)
    .pipe(maps.init())
    .pipe(concat('vendor.css'))
    .pipe(cleanCSS())
    .pipe(rename({
      basename: 'vendor',
      suffix: '.min'
    }))
    .pipe(maps.write('./'))
    .pipe(gulp.dest(paths.styles.dest));
};


var vendor_scripts = function () {
  var vendor_lines = fs.readFileSync('vendor-js.txt').toString().split('\n');
  var vendor_files = vendor_lines.filter(function (line) {
    return (!line.startsWith('-') && line.length > 0);
  });
  return gulp.src(vendor_files, { allowEmpty: true })
    .on('error', function (src) {
      console.error(src.message);
      this.emit('end');
    })
    .pipe(maps.init())
    .pipe(uglify())
    .pipe(concat('vendor.min.js'))
    .pipe(maps.write('./'))
    .pipe(gulp.dest(paths.scripts.dest))
    .pipe(touch);
};

/*
function watch() {
  gulp.watch(paths.scripts.src, scripts);
  gulp.watch(paths.styles.src, styles);
}
*/

var lint = function () {
  return gulp.src('plugin/**/*.js')
    .pipe(eslint())
    .pipe(eslint.format())
    .pipe(eslint.failAfterError());
};

//function _typescript() {
//  return tsProject.src({files: src + 'plugin/**/*.ts'})
//    .pipe(tsProject())
//    .js.pipe(gulp.dest('build/dist'));
//}

var typescript = function () {
  var tsResult = gulp.src(paths.typescript.src)
    .pipe(tsc());
  return tsResult.js.pipe(gulp.dest(paths.typescript.dest));
};

var ts_lint = function () {
  return gulp.src('plugin/js/**/*.ts')
    .pipe(tslint({
      formatter: 'verbose'
    }))
    .pipe(tslint.report());
};

var scripts = function () {
  return rollup({
    input: src + './main.js',
    sourcemap: true,
    format: 'es'
  }).on('error', e => {
    console.error(`${e.stack}`);
  })

    // point to the entry file and gives the name of the output file.
    .pipe(source('main.min.js', src))

    // buffer the output. most gulp plugins, including gulp-sourcemaps, don't support streams.
    .pipe(buffer())

    // tell gulp-sourcemaps to load the inline sourcemap produced by rollup-stream.
    .pipe(maps.init({ loadMaps: true }))
    // transform the code further here.
    /*
      .pipe(babel(
        {presets: [
          ['env', {
            targets: {
              'browsers': [
                'Chrome >= 52',
                'FireFox >= 44',
                'Safari >= 7',
                'Explorer 11',
                'last 4 Edge versions'
              ]
            },
            useBuiltIns: true,
            //debug: true
          }],
          'es2015'
        ],
        'ignore': [
          'node_modules'
        ]
        }
      ))
      */
    .pipe(ngAnnotate())
    //.pipe(gulpif(production, uglify()))
    .pipe(gulpif(production, terser()))
    .pipe(gulpif(production, insert.prepend(license)))
    // write the sourcemap alongside the output file.
    .pipe(maps.write('.'))

    // and output to ./dist/main.js as normal.
    .pipe(gulp.dest(paths.scripts.dest));
};

var test = function () {
  return gulp.src(['test/**/*.js'], { read: false })
    .pipe(mocha({ require: ['babel-core/register'], exit: true }))
    .on('error', console.error);
};

var build = gulp.series(
  clean,                          // removes the dist/ dir
  lint,                           // lints the .js
  gulp.parallel(vendor_styles, vendor_scripts, styles), // uglify and concat
  cleanup                         // remove .js that were converted from .ts
);
var vendor = gulp.parallel(vendor_styles, vendor_scripts);

exports.clean = clean;
exports.build = build;
exports.lint = lint;
exports.tslint = ts_lint;
exports.tsc = typescript;
exports.scripts = scripts;
exports.styles = styles;
exports.vendor = vendor;
exports.test = test;
exports.build = build;

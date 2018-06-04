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
  babel = require('gulp-babel'),
  concat = require('gulp-concat'),
  uglify = require('gulp-uglify'),
  ngAnnotate = require('gulp-ng-annotate'),
  rename = require('gulp-rename'),
  cleanCSS = require('gulp-clean-css'),
  del = require('del'),
  eslint = require('gulp-eslint'),
  maps = require('gulp-sourcemaps'),
  insert = require('gulp-insert'),
  fs = require('fs'),
  tsc = require('gulp-typescript'),
  tslint = require('gulp-tslint');
  //tsProject = tsc.createProject('tsconfig.json');

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
    src: [src + 'plugin/js/**/*.js', built_ts + '/**/*.js'],
    dest: 'dist/js/'
  }
};

function clean() {
  return del(['dist',built_ts ]);
}
function cleanup() {
  return del([built_ts]);
}
function styles() {
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
}
function vendor_styles() {
  var vendor_lines = fs.readFileSync('vendor-css.txt').toString().split('\n');
  var vendor_files = vendor_lines.filter( function (line) {
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
}

function scripts() {
  return gulp.src(paths.scripts.src, { sourcemaps: true })
    .pipe(babel({
      presets: [require.resolve('babel-preset-env')]
    }))
    .pipe(ngAnnotate())
    .pipe(maps.init())
    .pipe(uglify())
    .pipe(concat('dispatch.min.js'))
    .pipe(insert.prepend(license))
    .pipe(maps.write('./'))
    .pipe(gulp.dest(paths.scripts.dest));
}

function vendor_scripts() {
  var vendor_lines = fs.readFileSync('vendor-js.txt').toString().split('\n');
  var vendor_files = vendor_lines.filter( function (line) {
    return (!line.startsWith('-') && line.length > 0);
  });
  return gulp.src(vendor_files)
    .pipe(maps.init())
    .pipe(uglify())
    .pipe(concat('vendor.min.js'))
    .pipe(maps.write('./'))
    .pipe(gulp.dest(paths.scripts.dest));
}
function watch() {
  gulp.watch(paths.scripts.src, scripts);
  gulp.watch(paths.styles.src, styles);
}
function lint() {
  return gulp.src('plugin/**/*.js')
    .pipe(eslint())
    .pipe(eslint.format())
    .pipe(eslint.failAfterError());
}

//function _typescript() {
//  return tsProject.src({files: src + 'plugin/**/*.ts'})
//    .pipe(tsProject())
//    .js.pipe(gulp.dest('build/dist'));
//}

function typescript() {
  var tsResult = gulp.src(paths.typescript.src)
    .pipe(tsc());
  return tsResult.js.pipe(gulp.dest(paths.typescript.dest));
}

function ts_lint() {
  return gulp.src('plugin/js/**/*.ts')
    .pipe(tslint({
      formatter: 'verbose'
    }))
    .pipe(tslint.report());
}

var build = gulp.series(
  clean,                          // removes the dist/ dir
  gulp.parallel(lint, ts_lint),   // lints the .js, .ts files
  typescript,                     // converts .ts to .js
  gulp.parallel(vendor_styles, vendor_scripts, styles, scripts), // uglify and concat
  cleanup                         // remove .js that were converted from .ts
);

var vendor = gulp.parallel(vendor_styles, vendor_scripts);

exports.clean = clean;
exports.watch = watch;
exports.build = build;
exports.lint = lint;
exports.tslint = ts_lint;
exports.tsc = typescript;
exports.scripts = scripts;
exports.styles = styles;
exports.vendor = vendor;

gulp.task('default', build);

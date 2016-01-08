var StringHelpers;
(function (StringHelpers) {
    var dateRegex = /\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:/i;
    function isDate(str) {
        if (!angular.isString(str)) {
            return false;
        }
        return dateRegex.test(str);
    }
    StringHelpers.isDate = isDate;
    function obfusicate(str) {
        if (!angular.isString(str)) {
            return null;
        }
        return str.chars().map(function (c) {
            return '*';
        }).join('');
    }
    StringHelpers.obfusicate = obfusicate;
    function toString(obj) {
        if (!obj) {
            return '{ null }';
        }
        var answer = [];
        angular.forEach(obj, function (value, key) {
            var val = value;
            if (('' + key).toLowerCase() === 'password') {
                val = StringHelpers.obfusicate(value);
            }
            else if (angular.isObject(val)) {
                val = toString(val);
            }
            answer.push(key + ': ' + val);
        });
        return '{ ' + answer.join(', ') + ' }';
    }
    StringHelpers.toString = toString;
})(StringHelpers || (StringHelpers = {}));
var Core;
(function (Core) {
    function createConnectToServerOptions(options) {
        var defaults = {
            scheme: 'http',
            host: null,
            port: null,
            path: null,
            useProxy: true,
            jolokiaUrl: null,
            userName: null,
            password: null,
            view: null,
            name: null
        };
        var opts = options || {};
        return angular.extend(defaults, opts);
    }
    Core.createConnectToServerOptions = createConnectToServerOptions;
    function createConnectOptions(options) {
        return createConnectToServerOptions(options);
    }
    Core.createConnectOptions = createConnectOptions;
})(Core || (Core = {}));
var UrlHelpers;
(function (UrlHelpers) {
    var log = Logger.get("UrlHelpers");
    function noHash(url) {
        if (url.startsWith('#')) {
            return url.last(url.length - 1);
        }
        else {
            return url;
        }
    }
    UrlHelpers.noHash = noHash;
    function extractPath(url) {
        if (url.has('?')) {
            return url.split('?')[0];
        }
        else {
            return url;
        }
    }
    UrlHelpers.extractPath = extractPath;
    function contextActive(url, thingICareAbout) {
        var cleanUrl = extractPath(url);
        if (thingICareAbout.endsWith('/') && thingICareAbout.startsWith("/")) {
            return cleanUrl.has(thingICareAbout);
        }
        if (thingICareAbout.startsWith("/")) {
            return noHash(cleanUrl).startsWith(thingICareAbout);
        }
        return cleanUrl.endsWith(thingICareAbout);
    }
    UrlHelpers.contextActive = contextActive;
    function join() {
        var paths = [];
        for (var _i = 0; _i < arguments.length; _i++) {
            paths[_i - 0] = arguments[_i];
        }
        var tmp = [];
        var length = paths.length - 1;
        paths.forEach(function (path, index) {
            if (Core.isBlank(path)) {
                return;
            }
            if (index !== 0 && path.first(1) === '/') {
                path = path.slice(1);
            }
            if (index !== length && path.last(1) === '/') {
                path = path.slice(0, path.length - 1);
            }
            if (!Core.isBlank(path)) {
                tmp.push(path);
            }
        });
        var rc = tmp.join('/');
        return rc;
    }
    UrlHelpers.join = join;
    UrlHelpers.parseQueryString = hawtioPluginLoader.parseQueryString;
    function maybeProxy(jolokiaUrl, url) {
        if (jolokiaUrl && jolokiaUrl.startsWith('proxy/')) {
            log.debug("Jolokia URL is proxied, applying proxy to: ", url);
            return join('proxy', url);
        }
        var origin = window.location['origin'];
        if (url && (url.startsWith('http') && !url.startsWith(origin))) {
            log.debug("Url doesn't match page origin: ", origin, " applying proxy to: ", url);
            return join('proxy', url);
        }
        log.debug("No need to proxy: ", url);
        return url;
    }
    UrlHelpers.maybeProxy = maybeProxy;
    function escapeColons(url) {
        var answer = url;
        if (url.startsWith('proxy')) {
            answer = url.replace(/:/g, '\\:');
        }
        else {
            answer = url.replace(/:([^\/])/, '\\:$1');
        }
        return answer;
    }
    UrlHelpers.escapeColons = escapeColons;
})(UrlHelpers || (UrlHelpers = {}));
var Core;
(function (Core) {
    Core.injector = null;
    var _urlPrefix = null;
    Core.connectionSettingsKey = "jvmConnect";
    function _resetUrlPrefix() {
        _urlPrefix = null;
    }
    Core._resetUrlPrefix = _resetUrlPrefix;
    function url(path) {
        if (path) {
            if (path.startsWith && path.startsWith("/")) {
                if (!_urlPrefix) {
                    _urlPrefix = $('base').attr('href') || "";
                    if (_urlPrefix.endsWith && _urlPrefix.endsWith('/')) {
                        _urlPrefix = _urlPrefix.substring(0, _urlPrefix.length - 1);
                    }
                }
                if (_urlPrefix) {
                    return _urlPrefix + path;
                }
            }
        }
        return path;
    }
    Core.url = url;
    function windowLocation() {
        return window.location;
    }
    Core.windowLocation = windowLocation;
    String.prototype.unescapeHTML = function () {
        var txt = document.createElement("textarea");
        txt.innerHTML = this;
        return txt.value;
    };
    if (!Object.keys) {
        console.debug("Creating hawt.io version of Object.keys()");
        Object.keys = function (obj) {
            var keys = [], k;
            for (k in obj) {
                if (Object.prototype.hasOwnProperty.call(obj, k)) {
                    keys.push(k);
                }
            }
            return keys;
        };
    }
    function _resetJolokiaUrls() {
        jolokiaUrls = [
            Core.url("jolokia"),
            "/jolokia"
        ];
        return jolokiaUrls;
    }
    Core._resetJolokiaUrls = _resetJolokiaUrls;
    var jolokiaUrls = Core._resetJolokiaUrls();
    function trimLeading(text, prefix) {
        if (text && prefix) {
            if (text.startsWith(prefix)) {
                return text.substring(prefix.length);
            }
        }
        return text;
    }
    Core.trimLeading = trimLeading;
    function trimTrailing(text, postfix) {
        if (text && postfix) {
            if (text.endsWith(postfix)) {
                return text.substring(0, text.length - postfix.length);
            }
        }
        return text;
    }
    Core.trimTrailing = trimTrailing;
    function loadConnectionMap() {
        var localStorage = Core.getLocalStorage();
        try {
            var answer = angular.fromJson(localStorage[Core.connectionSettingsKey]);
            if (!answer) {
                return {};
            }
            else {
                return answer;
            }
        }
        catch (e) {
            delete localStorage[Core.connectionSettingsKey];
            return {};
        }
    }
    Core.loadConnectionMap = loadConnectionMap;
    function saveConnectionMap(map) {
        Logger.get("Core").debug("Saving connection map: ", StringHelpers.toString(map));
        localStorage[Core.connectionSettingsKey] = angular.toJson(map);
    }
    Core.saveConnectionMap = saveConnectionMap;
    function getConnectOptions(name, localStorage) {
        if (localStorage === void 0) { localStorage = Core.getLocalStorage(); }
        if (!name) {
            return null;
        }
        return Core.loadConnectionMap()[name];
    }
    Core.getConnectOptions = getConnectOptions;
    Core.ConnectionName = null;
    function getConnectionNameParameter(search) {
        if (Core.ConnectionName) {
            return Core.ConnectionName;
        }
        var connectionName = undefined;
        if ('con' in window) {
            connectionName = window['con'];
            Logger.get("Core").debug("Found connection name from window: ", connectionName);
        }
        else {
            connectionName = search["con"];
            if (angular.isArray(connectionName)) {
                connectionName = connectionName[0];
            }
            if (connectionName) {
                connectionName = connectionName.unescapeURL();
                Logger.get("Core").debug("Found connection name from URL: ", connectionName);
            }
            else {
                Logger.get("Core").debug("No connection name found, using direct connection to JVM");
            }
        }
        Core.ConnectionName = connectionName;
        return connectionName;
    }
    Core.getConnectionNameParameter = getConnectionNameParameter;
    function createServerConnectionUrl(options) {
        Logger.get("Core").debug("Connect to server, options: ", StringHelpers.toString(options));
        var answer = null;
        if (options.jolokiaUrl) {
            answer = options.jolokiaUrl;
        }
        if (answer === null) {
            answer = options.scheme || 'http';
            answer += '://' + (options.host || 'localhost');
            if (options.port) {
                answer += ':' + options.port;
            }
            if (options.path) {
                answer = UrlHelpers.join(answer, options.path);
            }
        }
        if (options.useProxy) {
            answer = UrlHelpers.join('proxy', answer);
        }
        Logger.get("Core").debug("Using URL: ", answer);
        return answer;
    }
    Core.createServerConnectionUrl = createServerConnectionUrl;
    function getJolokiaUrl() {
        var query = hawtioPluginLoader.parseQueryString();
        var localMode = query['localMode'];
        if (localMode) {
            Logger.get("Core").debug("local mode so not using jolokia URL");
            jolokiaUrls = [];
            return null;
        }
        var uri = null;
        var connectionName = Core.getConnectionNameParameter(query);
        if (connectionName) {
            var connectOptions = Core.getConnectOptions(connectionName);
            if (connectOptions) {
                uri = createServerConnectionUrl(connectOptions);
                Logger.get("Core").debug("Using jolokia URI: ", uri, " from local storage");
            }
            else {
                Logger.get("Core").debug("Connection parameter found but no stored connections under name: ", connectionName);
            }
        }
        if (!uri) {
            var fakeCredentials = {
                username: 'public',
                password: 'biscuit'
            };
            var localStorage = getLocalStorage();
            if ('userDetails' in window) {
                fakeCredentials = window['userDetails'];
            }
            else if ('userDetails' in localStorage) {
                fakeCredentials = angular.fromJson(localStorage['userDetails']);
            }
            uri = jolokiaUrls.find(function (url) {
                var jqxhr = $.ajax(url, {
                    async: false,
                    username: fakeCredentials.username,
                    password: fakeCredentials.password
                });
                return jqxhr.status === 200 || jqxhr.status === 401 || jqxhr.status === 403;
            });
            Logger.get("Core").debug("Using jolokia URI: ", uri, " via discovery");
        }
        return uri;
    }
    Core.getJolokiaUrl = getJolokiaUrl;
    function adjustHeight() {
        var windowHeight = $(window).height();
        var headerHeight = $("#main-nav").height();
        var containerHeight = windowHeight - headerHeight;
        $("#main").css("min-height", "" + containerHeight + "px");
    }
    Core.adjustHeight = adjustHeight;
    function isChromeApp() {
        var answer = false;
        try {
            answer = (chrome && chrome.app && chrome.extension) ? true : false;
        }
        catch (e) {
            answer = false;
        }
        return answer;
    }
    Core.isChromeApp = isChromeApp;
    function addCSS(path) {
        if ('createStyleSheet' in document) {
            document.createStyleSheet(path);
        }
        else {
            var link = $("<link>");
            $("head").append(link);
            link.attr({
                rel: 'stylesheet',
                type: 'text/css',
                href: path
            });
        }
    }
    Core.addCSS = addCSS;
    var dummyStorage = {};
    function getLocalStorage() {
        var storage = window.localStorage || (function () {
            return dummyStorage;
        })();
        return storage;
    }
    Core.getLocalStorage = getLocalStorage;
    function asArray(value) {
        return angular.isArray(value) ? value : [value];
    }
    Core.asArray = asArray;
    function parseBooleanValue(value, defaultValue) {
        if (defaultValue === void 0) { defaultValue = false; }
        if (!angular.isDefined(value) || !value) {
            return defaultValue;
        }
        if (value.constructor === Boolean) {
            return value;
        }
        if (angular.isString(value)) {
            switch (value.toLowerCase()) {
                case "true":
                case "1":
                case "yes":
                    return true;
                default:
                    return false;
            }
        }
        if (angular.isNumber(value)) {
            return value !== 0;
        }
        throw new Error("Can't convert value " + value + " to boolean");
    }
    Core.parseBooleanValue = parseBooleanValue;
    function toString(value) {
        if (angular.isNumber(value)) {
            return numberToString(value);
        }
        else {
            return angular.toJson(value, true);
        }
    }
    Core.toString = toString;
    function booleanToString(value) {
        return "" + value;
    }
    Core.booleanToString = booleanToString;
    function parseIntValue(value, description) {
        if (description === void 0) { description = "integer"; }
        if (angular.isString(value)) {
            try {
                return parseInt(value);
            }
            catch (e) {
                console.log("Failed to parse " + description + " with text '" + value + "'");
            }
        }
        else if (angular.isNumber(value)) {
            return value;
        }
        return null;
    }
    Core.parseIntValue = parseIntValue;
    function numberToString(value) {
        return "" + value;
    }
    Core.numberToString = numberToString;
    function parseFloatValue(value, description) {
        if (description === void 0) { description = "float"; }
        if (angular.isString(value)) {
            try {
                return parseFloat(value);
            }
            catch (e) {
                console.log("Failed to parse " + description + " with text '" + value + "'");
            }
        }
        else if (angular.isNumber(value)) {
            return value;
        }
        return null;
    }
    Core.parseFloatValue = parseFloatValue;
    function pathGet(object, paths) {
        var pathArray = (angular.isArray(paths)) ? paths : (paths || "").split(".");
        var value = object;
        angular.forEach(pathArray, function (name) {
            if (value) {
                try {
                    value = value[name];
                }
                catch (e) {
                    return null;
                }
            }
            else {
                return null;
            }
        });
        return value;
    }
    Core.pathGet = pathGet;
    function pathSet(object, paths, newValue) {
        var pathArray = (angular.isArray(paths)) ? paths : (paths || "").split(".");
        var value = object;
        var lastIndex = pathArray.length - 1;
        angular.forEach(pathArray, function (name, idx) {
            var next = value[name];
            if (idx >= lastIndex || !angular.isObject(next)) {
                next = (idx < lastIndex) ? {} : newValue;
                value[name] = next;
            }
            value = next;
        });
        return value;
    }
    Core.pathSet = pathSet;
    function $applyNowOrLater($scope) {
        if ($scope.$$phase || $scope.$root.$$phase) {
            setTimeout(function () {
                Core.$apply($scope);
            }, 50);
        }
        else {
            $scope.$apply();
        }
    }
    Core.$applyNowOrLater = $applyNowOrLater;
    function $applyLater($scope, timeout) {
        if (timeout === void 0) { timeout = 50; }
        setTimeout(function () {
            Core.$apply($scope);
        }, timeout);
    }
    Core.$applyLater = $applyLater;
    function $apply($scope) {
        var phase = $scope.$$phase || $scope.$root.$$phase;
        if (!phase) {
            $scope.$apply();
        }
    }
    Core.$apply = $apply;
    function $digest($scope) {
        var phase = $scope.$$phase || $scope.$root.$$phase;
        if (!phase) {
            $scope.$digest();
        }
    }
    Core.$digest = $digest;
    function getOrCreateElements(domElement, arrayOfElementNames) {
        var element = domElement;
        angular.forEach(arrayOfElementNames, function (name) {
            if (element) {
                var children = $(element).children(name);
                if (!children || !children.length) {
                    $("<" + name + "></" + name + ">").appendTo(element);
                    children = $(element).children(name);
                }
                element = children;
            }
        });
        return element;
    }
    Core.getOrCreateElements = getOrCreateElements;
    var _escapeHtmlChars = {
        "#": "&#35;",
        "'": "&#39;",
        "<": "&lt;",
        ">": "&gt;",
        "\"": "&quot;"
    };
    function unescapeHtml(str) {
        angular.forEach(_escapeHtmlChars, function (value, key) {
            var regex = new RegExp(value, "g");
            str = str.replace(regex, key);
        });
        str = str.replace(/&gt;/g, ">");
        return str;
    }
    Core.unescapeHtml = unescapeHtml;
    function escapeHtml(str) {
        if (angular.isString(str)) {
            var newStr = "";
            for (var i = 0; i < str.length; i++) {
                var ch = str.charAt(i);
                var ch = _escapeHtmlChars[ch] || ch;
                newStr += ch;
            }
            return newStr;
        }
        else {
            return str;
        }
    }
    Core.escapeHtml = escapeHtml;
    function isBlank(str) {
        if (str === undefined || str === null) {
            return true;
        }
        if (angular.isString(str)) {
            return str.isBlank();
        }
        else {
            return false;
        }
    }
    Core.isBlank = isBlank;
    function notification(type, message, options) {
        if (options === void 0) { options = null; }
        if (options === null) {
            options = {};
        }
        if (type === 'error' || type === 'warning') {
            if (!angular.isDefined(options.onclick)) {
                options.onclick = window['showLogPanel'];
            }
        }
        toastr[type](message, '', options);
    }
    Core.notification = notification;
    function clearNotifications() {
        toastr.clear();
    }
    Core.clearNotifications = clearNotifications;
    function trimQuotes(text) {
        if (text) {
            while (text.endsWith('"') || text.endsWith("'")) {
                text = text.substring(0, text.length - 1);
            }
            while (text.startsWith('"') || text.startsWith("'")) {
                text = text.substring(1, text.length);
            }
        }
        return text;
    }
    Core.trimQuotes = trimQuotes;
    function humanizeValue(value) {
        if (value) {
            var text = value + '';
            try {
                text = text.underscore();
            }
            catch (e) {
            }
            try {
                text = text.humanize();
            }
            catch (e) {
            }
            return trimQuotes(text);
        }
        return value;
    }
    Core.humanizeValue = humanizeValue;
})(Core || (Core = {}));
var ControllerHelpers;
(function (ControllerHelpers) {
    var log = Logger.get("ControllerHelpers");
    function createClassSelector(config) {
        return function (selector, model) {
            if (selector === model && selector in config) {
                return config[selector];
            }
            return '';
        };
    }
    ControllerHelpers.createClassSelector = createClassSelector;
    function createValueClassSelector(config) {
        return function (model) {
            if (model in config) {
                return config[model];
            }
            else {
                return '';
            }
        };
    }
    ControllerHelpers.createValueClassSelector = createValueClassSelector;
    function bindModelToSearchParam($scope, $location, modelName, paramName, initialValue, to, from) {
        if (!(modelName in $scope)) {
            $scope[modelName] = initialValue;
        }
        var toConverter = to || Core.doNothing;
        var fromConverter = from || Core.doNothing;
        function currentValue() {
            return fromConverter($location.search()[paramName] || initialValue);
        }
        var value = currentValue();
        Core.pathSet($scope, modelName, value);
        $scope.$watch(modelName, function (newValue, oldValue) {
            if (newValue !== oldValue) {
                if (newValue !== undefined && newValue !== null) {
                    $location.search(paramName, toConverter(newValue));
                }
                else {
                    $location.search(paramName, '');
                }
            }
        });
    }
    ControllerHelpers.bindModelToSearchParam = bindModelToSearchParam;
    function reloadWhenParametersChange($route, $scope, $location, parameters) {
        if (parameters === void 0) { parameters = ["nid"]; }
        var initial = angular.copy($location.search());
        $scope.$on('$routeUpdate', function () {
            var current = $location.search();
            var changed = [];
            angular.forEach(parameters, function (param) {
                if (current[param] !== initial[param]) {
                    changed.push(param);
                }
            });
            if (changed.length) {
                $route.reload();
            }
        });
    }
    ControllerHelpers.reloadWhenParametersChange = reloadWhenParametersChange;
})(ControllerHelpers || (ControllerHelpers = {}));
var __extends = this.__extends || function (d, b) {
    for (var p in b) if (b.hasOwnProperty(p)) d[p] = b[p];
    function __() { this.constructor = d; }
    __.prototype = b.prototype;
    d.prototype = new __();
};
var Core;
(function (Core) {
    var log = Logger.get("Core");
    var TasksImpl = (function () {
        function TasksImpl() {
            this.tasks = {};
            this.tasksExecuted = false;
            this._onComplete = null;
        }
        TasksImpl.prototype.addTask = function (name, task) {
            this.tasks[name] = task;
            if (this.tasksExecuted) {
                this.executeTask(name, task);
            }
        };
        TasksImpl.prototype.executeTask = function (name, task) {
            if (angular.isFunction(task)) {
                log.debug("Executing task : ", name);
                try {
                    task();
                }
                catch (error) {
                    log.debug("Failed to execute task: ", name, " error: ", error);
                }
            }
        };
        TasksImpl.prototype.onComplete = function (cb) {
            this._onComplete = cb;
        };
        TasksImpl.prototype.execute = function () {
            var _this = this;
            if (this.tasksExecuted) {
                return;
            }
            angular.forEach(this.tasks, function (task, name) {
                _this.executeTask(name, task);
            });
            this.tasksExecuted = true;
            if (angular.isFunction(this._onComplete)) {
                this._onComplete();
            }
        };
        TasksImpl.prototype.reset = function () {
            this.tasksExecuted = false;
        };
        return TasksImpl;
    })();
    Core.TasksImpl = TasksImpl;
    var ParameterizedTasksImpl = (function (_super) {
        __extends(ParameterizedTasksImpl, _super);
        function ParameterizedTasksImpl() {
            var _this = this;
            _super.call(this);
            this.tasks = {};
            this.onComplete(function () {
                _this.reset();
            });
        }
        ParameterizedTasksImpl.prototype.addTask = function (name, task) {
            this.tasks[name] = task;
        };
        ParameterizedTasksImpl.prototype.execute = function () {
            var _this = this;
            var params = [];
            for (var _i = 0; _i < arguments.length; _i++) {
                params[_i - 0] = arguments[_i];
            }
            if (this.tasksExecuted) {
                return;
            }
            var theArgs = params;
            var keys = Object.keys(this.tasks);
            keys.forEach(function (name) {
                var task = _this.tasks[name];
                if (angular.isFunction(task)) {
                    log.debug("Executing task: ", name, " with parameters: ", theArgs);
                    try {
                        task.apply(task, theArgs);
                    }
                    catch (e) {
                        log.debug("Failed to execute task: ", name, " error: ", e);
                    }
                }
            });
            this.tasksExecuted = true;
            if (angular.isFunction(this._onComplete)) {
                this._onComplete();
            }
        };
        return ParameterizedTasksImpl;
    })(TasksImpl);
    Core.ParameterizedTasksImpl = ParameterizedTasksImpl;
    Core.postLoginTasks = new Core.TasksImpl();
    Core.preLogoutTasks = new Core.TasksImpl();
})(Core || (Core = {}));
var Core;
(function (Core) {
    function operationToString(name, args) {
        if (!args || args.length === 0) {
            return name + '()';
        }
        else {
            return name + '(' + args.map(function (arg) {
                if (angular.isString(arg)) {
                    arg = angular.fromJson(arg);
                }
                return arg.type;
            }).join(',') + ')';
        }
    }
    Core.operationToString = operationToString;
})(Core || (Core = {}));
var Core;
(function (Core) {
    var Folder = (function () {
        function Folder(title) {
            this.title = title;
            this.key = null;
            this.typeName = null;
            this.children = [];
            this.folderNames = [];
            this.domain = null;
            this.objectName = null;
            this.map = {};
            this.entries = {};
            this.addClass = null;
            this.parent = null;
            this.isLazy = false;
            this.icon = null;
            this.tooltip = null;
            this.entity = null;
            this.version = null;
            this.mbean = null;
            this.addClass = escapeTreeCssStyles(title);
        }
        Folder.prototype.get = function (key) {
            return this.map[key];
        };
        Folder.prototype.isFolder = function () {
            return this.children.length > 0;
        };
        Folder.prototype.navigate = function () {
            var paths = [];
            for (var _i = 0; _i < arguments.length; _i++) {
                paths[_i - 0] = arguments[_i];
            }
            var node = this;
            paths.forEach(function (path) {
                if (node) {
                    node = node.get(path);
                }
            });
            return node;
        };
        Folder.prototype.hasEntry = function (key, value) {
            var entries = this.entries;
            if (entries) {
                var actual = entries[key];
                return actual && value === actual;
            }
            return false;
        };
        Folder.prototype.parentHasEntry = function (key, value) {
            if (this.parent) {
                return this.parent.hasEntry(key, value);
            }
            return false;
        };
        Folder.prototype.ancestorHasEntry = function (key, value) {
            var parent = this.parent;
            while (parent) {
                if (parent.hasEntry(key, value))
                    return true;
                parent = parent.parent;
            }
            return false;
        };
        Folder.prototype.ancestorHasType = function (typeName) {
            var parent = this.parent;
            while (parent) {
                if (typeName === parent.typeName)
                    return true;
                parent = parent.parent;
            }
            return false;
        };
        Folder.prototype.getOrElse = function (key, defaultValue) {
            if (defaultValue === void 0) { defaultValue = new Folder(key); }
            var answer = this.map[key];
            if (!answer) {
                answer = defaultValue;
                this.map[key] = answer;
                this.children.push(answer);
                answer.parent = this;
            }
            return answer;
        };
        Folder.prototype.sortChildren = function (recursive) {
            var children = this.children;
            if (children) {
                this.children = children.sortBy("title");
                if (recursive) {
                    angular.forEach(children, function (child) { return child.sortChildren(recursive); });
                }
            }
        };
        Folder.prototype.moveChild = function (child) {
            if (child && child.parent !== this) {
                child.detach();
                child.parent = this;
                this.children.push(child);
            }
        };
        Folder.prototype.insertBefore = function (child, referenceFolder) {
            child.detach();
            child.parent = this;
            var idx = _.indexOf(this.children, referenceFolder);
            if (idx >= 0) {
                this.children.splice(idx, 0, child);
            }
        };
        Folder.prototype.insertAfter = function (child, referenceFolder) {
            child.detach();
            child.parent = this;
            var idx = _.indexOf(this.children, referenceFolder);
            if (idx >= 0) {
                this.children.splice(idx + 1, 0, child);
            }
        };
        Folder.prototype.detach = function () {
            var oldParent = this.parent;
            if (oldParent) {
                var oldParentChildren = oldParent.children;
                if (oldParentChildren) {
                    var idx = oldParentChildren.indexOf(this);
                    if (idx < 0) {
                        oldParent.children = oldParent.children.remove({ key: this.key });
                    }
                    else {
                        oldParentChildren.splice(idx, 1);
                    }
                }
                this.parent = null;
            }
        };
        Folder.prototype.findDescendant = function (filter) {
            if (filter(this)) {
                return this;
            }
            var answer = null;
            angular.forEach(this.children, function (child) {
                if (!answer) {
                    answer = child.findDescendant(filter);
                }
            });
            return answer;
        };
        Folder.prototype.findAncestor = function (filter) {
            if (filter(this)) {
                return this;
            }
            if (this.parent != null) {
                return this.parent.findAncestor(filter);
            }
            else {
                return null;
            }
        };
        return Folder;
    })();
    Core.Folder = Folder;
})(Core || (Core = {}));
;
var Folder = (function (_super) {
    __extends(Folder, _super);
    function Folder() {
        _super.apply(this, arguments);
    }
    return Folder;
})(Core.Folder);
;
var Core;
(function (Core) {
    var log = Logger.get("Core");
    var Workspace = (function () {
        function Workspace(jolokia, jolokiaStatus, jmxTreeLazyLoadRegistry, $location, $compile, $templateCache, localStorage, $rootScope, userDetails) {
            this.jolokia = jolokia;
            this.jolokiaStatus = jolokiaStatus;
            this.jmxTreeLazyLoadRegistry = jmxTreeLazyLoadRegistry;
            this.$location = $location;
            this.$compile = $compile;
            this.$templateCache = $templateCache;
            this.localStorage = localStorage;
            this.$rootScope = $rootScope;
            this.userDetails = userDetails;
            this.operationCounter = 0;
            this.tree = new Core.Folder('MBeans');
            this.mbeanTypesToDomain = {};
            this.mbeanServicesToDomain = {};
            this.attributeColumnDefs = {};
            this.treePostProcessors = [];
            this.topLevelTabs = [];
            this.subLevelTabs = [];
            this.keyToNodeMap = {};
            this.pluginRegisterHandle = null;
            this.pluginUpdateCounter = null;
            this.treeWatchRegisterHandle = null;
            this.treeWatcherCounter = null;
            this.treeElement = null;
            this.mapData = {};
            if (!('autoRefresh' in localStorage)) {
                localStorage['autoRefresh'] = true;
            }
            if (!('updateRate' in localStorage)) {
                localStorage['updateRate'] = 5000;
            }
        }
        Workspace.prototype.createChildWorkspace = function (location) {
            var child = new Workspace(this.jolokia, this.jolokiaStatus, this.jmxTreeLazyLoadRegistry, this.$location, this.$compile, this.$templateCache, this.localStorage, this.$rootScope, this.userDetails);
            angular.forEach(this, function (value, key) { return child[key] = value; });
            child.$location = location;
            return child;
        };
        Workspace.prototype.getLocalStorage = function (key) {
            return this.localStorage[key];
        };
        Workspace.prototype.setLocalStorage = function (key, value) {
            this.localStorage[key] = value;
        };
        Workspace.prototype.loadTree = function () {
            var flags = { ignoreErrors: true, maxDepth: 7 };
            var data = this.jolokia.list(null, onSuccess(null, flags));
            if (data) {
                this.jolokiaStatus.xhr = null;
            }
            this.populateTree({
                value: data
            });
        };
        Workspace.prototype.addTreePostProcessor = function (processor) {
            this.treePostProcessors.push(processor);
            var tree = this.tree;
            if (tree) {
                processor(tree);
            }
        };
        Workspace.prototype.maybeMonitorPlugins = function () {
            if (this.treeContainsDomainAndProperties("hawtio", { type: "Registry" })) {
                if (this.pluginRegisterHandle === null) {
                    this.pluginRegisterHandle = this.jolokia.register(angular.bind(this, this.maybeUpdatePlugins), {
                        type: "read",
                        mbean: "hawtio:type=Registry",
                        attribute: "UpdateCounter"
                    });
                }
            }
            else {
                if (this.pluginRegisterHandle !== null) {
                    this.jolokia.unregister(this.pluginRegisterHandle);
                    this.pluginRegisterHandle = null;
                    this.pluginUpdateCounter = null;
                }
            }
            if (this.treeContainsDomainAndProperties("hawtio", { type: "TreeWatcher" })) {
                if (this.treeWatchRegisterHandle === null) {
                    this.treeWatchRegisterHandle = this.jolokia.register(angular.bind(this, this.maybeReloadTree), {
                        type: "read",
                        mbean: "hawtio:type=TreeWatcher",
                        attribute: "Counter"
                    });
                }
            }
        };
        Workspace.prototype.maybeUpdatePlugins = function (response) {
            if (this.pluginUpdateCounter === null) {
                this.pluginUpdateCounter = response.value;
                return;
            }
            if (this.pluginUpdateCounter !== response.value) {
                if (Core.parseBooleanValue(localStorage['autoRefresh'])) {
                    window.location.reload();
                }
            }
        };
        Workspace.prototype.maybeReloadTree = function (response) {
            var counter = response.value;
            if (this.treeWatcherCounter === null) {
                this.treeWatcherCounter = counter;
                return;
            }
            if (this.treeWatcherCounter !== counter) {
                this.treeWatcherCounter = counter;
                var workspace = this;
                function wrapInValue(response) {
                    var wrapper = {
                        value: response
                    };
                    workspace.populateTree(wrapper);
                }
                this.jolokia.list(null, onSuccess(wrapInValue, { ignoreErrors: true, maxDepth: 2 }));
            }
        };
        Workspace.prototype.folderGetOrElse = function (folder, value) {
            if (folder) {
                try {
                    return folder.getOrElse(value);
                }
                catch (e) {
                    log.warn("Failed to find value " + value + " on folder " + folder);
                }
            }
            return null;
        };
        Workspace.prototype.populateTree = function (response) {
            log.debug("JMX tree has been loaded, data: ", response.value);
            var rootId = 'root';
            var separator = '-';
            this.mbeanTypesToDomain = {};
            this.mbeanServicesToDomain = {};
            this.keyToNodeMap = {};
            var tree = new Core.Folder('MBeans');
            tree.key = rootId;
            var domains = response.value;
            for (var domainName in domains) {
                var domainClass = escapeDots(domainName);
                var domain = domains[domainName];
                for (var mbeanName in domain) {
                    var entries = {};
                    var folder = this.folderGetOrElse(tree, domainName);
                    folder.domain = domainName;
                    if (!folder.key) {
                        folder.key = rootId + separator + domainName;
                    }
                    var folderNames = [domainName];
                    folder.folderNames = folderNames;
                    folderNames = folderNames.clone();
                    var items = mbeanName.split(',');
                    var paths = [];
                    var typeName = null;
                    var serviceName = null;
                    items.forEach(function (item) {
                        var kv = item.split('=');
                        var key = kv[0];
                        var value = kv[1] || key;
                        entries[key] = value;
                        var moveToFront = false;
                        var lowerKey = key.toLowerCase();
                        if (lowerKey === "type") {
                            typeName = value;
                            if (folder.map[value]) {
                                moveToFront = true;
                            }
                        }
                        if (lowerKey === "service") {
                            serviceName = value;
                        }
                        if (moveToFront) {
                            paths.splice(0, 0, value);
                        }
                        else {
                            paths.push(value);
                        }
                    });
                    var configureFolder = function (folder, name) {
                        folder.domain = domainName;
                        if (!folder.key) {
                            folder.key = rootId + separator + folderNames.join(separator);
                        }
                        this.keyToNodeMap[folder.key] = folder;
                        folder.folderNames = folderNames.clone();
                        var classes = "";
                        var entries = folder.entries;
                        var entryKeys = Object.keys(entries).filter(function (n) { return n.toLowerCase().indexOf("type") >= 0; });
                        if (entryKeys.length) {
                            angular.forEach(entryKeys, function (entryKey) {
                                var entryValue = entries[entryKey];
                                if (!folder.ancestorHasEntry(entryKey, entryValue)) {
                                    classes += " " + domainClass + separator + entryValue;
                                }
                            });
                        }
                        else {
                            var kindName = folderNames.last();
                            if (kindName === name) {
                                kindName += "-folder";
                            }
                            if (kindName) {
                                classes += " " + domainClass + separator + kindName;
                            }
                        }
                        folder.addClass = escapeTreeCssStyles(classes);
                        return folder;
                    };
                    var lastPath = paths.pop();
                    var ws = this;
                    paths.forEach(function (value) {
                        folder = ws.folderGetOrElse(folder, value);
                        if (folder) {
                            folderNames.push(value);
                            angular.bind(ws, configureFolder, folder, value)();
                        }
                    });
                    var key = rootId + separator + folderNames.join(separator) + separator + lastPath;
                    var objectName = domainName + ":" + mbeanName;
                    if (folder) {
                        folder = this.folderGetOrElse(folder, lastPath);
                        if (folder) {
                            folder.entries = entries;
                            folder.key = key;
                            angular.bind(this, configureFolder, folder, lastPath)();
                            folder.title = Core.trimQuotes(lastPath);
                            folder.objectName = objectName;
                            folder.mbean = domain[mbeanName];
                            folder.typeName = typeName;
                            var addFolderByDomain = function (owner, typeName) {
                                var map = owner[typeName];
                                if (!map) {
                                    map = {};
                                    owner[typeName] = map;
                                }
                                var value = map[domainName];
                                if (!value) {
                                    map[domainName] = folder;
                                }
                                else {
                                    var array = null;
                                    if (angular.isArray(value)) {
                                        array = value;
                                    }
                                    else {
                                        array = [value];
                                        map[domainName] = array;
                                    }
                                    array.push(folder);
                                }
                            };
                            if (serviceName) {
                                angular.bind(this, addFolderByDomain, this.mbeanServicesToDomain, serviceName)();
                            }
                            if (typeName) {
                                angular.bind(this, addFolderByDomain, this.mbeanTypesToDomain, typeName)();
                            }
                        }
                    }
                    else {
                        log.info("No folder found for lastPath: " + lastPath);
                    }
                }
                tree.sortChildren(true);
                this.enableLazyLoading(tree);
                this.tree = tree;
                var processors = this.treePostProcessors;
                angular.forEach(processors, function (processor) { return processor(tree); });
                this.maybeMonitorPlugins();
                var rootScope = this.$rootScope;
                if (rootScope) {
                    rootScope.$broadcast('jmxTreeUpdated');
                }
            }
        };
        Workspace.prototype.enableLazyLoading = function (folder) {
            var _this = this;
            var children = folder.children;
            if (children && children.length) {
                angular.forEach(children, function (child) {
                    _this.enableLazyLoading(child);
                });
            }
            else {
                var lazyFunction = Jmx.findLazyLoadingFunction(this, folder);
                if (lazyFunction) {
                    folder.isLazy = true;
                }
            }
        };
        Workspace.prototype.hash = function () {
            var hash = this.$location.search();
            var params = Core.hashToString(hash);
            if (params) {
                return "?" + params;
            }
            return "";
        };
        Workspace.prototype.getActiveTab = function () {
            var workspace = this;
            return this.topLevelTabs.find(function (tab) {
                if (!angular.isDefined(tab.isActive)) {
                    return workspace.isLinkActive(tab.href());
                }
                else {
                    return tab.isActive(workspace);
                }
            });
        };
        Workspace.prototype.getStrippedPathName = function () {
            var pathName = Core.trimLeading((this.$location.path() || '/'), "#");
            pathName = Core.trimLeading(pathName, "/");
            return pathName;
        };
        Workspace.prototype.linkContains = function () {
            var words = [];
            for (var _i = 0; _i < arguments.length; _i++) {
                words[_i - 0] = arguments[_i];
            }
            var pathName = this.getStrippedPathName();
            return words.all(function (word) {
                return pathName.has(word);
            });
        };
        Workspace.prototype.isLinkActive = function (href) {
            var pathName = this.getStrippedPathName();
            var link = Core.trimLeading(href, "#");
            link = Core.trimLeading(link, "/");
            var idx = link.indexOf('?');
            if (idx >= 0) {
                link = link.substring(0, idx);
            }
            if (!pathName.length) {
                return link === pathName;
            }
            else {
                return pathName.startsWith(link);
            }
        };
        Workspace.prototype.isLinkPrefixActive = function (href) {
            var pathName = this.getStrippedPathName();
            var link = Core.trimLeading(href, "#");
            link = Core.trimLeading(link, "/");
            var idx = link.indexOf('?');
            if (idx >= 0) {
                link = link.substring(0, idx);
            }
            return pathName.startsWith(link);
        };
        Workspace.prototype.isTopTabActive = function (path) {
            var tab = this.$location.search()['tab'];
            if (angular.isString(tab)) {
                return tab.startsWith(path);
            }
            return this.isLinkActive(path);
        };
        Workspace.prototype.getSelectedMBeanName = function () {
            var selection = this.selection;
            if (selection) {
                return selection.objectName;
            }
            return null;
        };
        Workspace.prototype.validSelection = function (uri) {
            var workspace = this;
            var filter = function (t) {
                var fn = t.href;
                if (fn) {
                    var href = fn();
                    if (href) {
                        if (href.startsWith("#/")) {
                            href = href.substring(2);
                        }
                        return href === uri;
                    }
                }
                return false;
            };
            var tab = this.subLevelTabs.find(filter);
            if (!tab) {
                tab = this.topLevelTabs.find(filter);
            }
            if (tab) {
                var validFn = tab['isValid'];
                return !angular.isDefined(validFn) || validFn(workspace);
            }
            else {
                log.info("Could not find tab for " + uri);
                return false;
            }
        };
        Workspace.prototype.removeAndSelectParentNode = function () {
            var selection = this.selection;
            if (selection) {
                var parent = selection.parent;
                if (parent) {
                    var idx = parent.children.indexOf(selection);
                    if (idx < 0) {
                        idx = parent.children.findIndex(function (n) { return n.key === selection.key; });
                    }
                    if (idx >= 0) {
                        parent.children.splice(idx, 1);
                    }
                    this.updateSelectionNode(parent);
                }
            }
        };
        Workspace.prototype.selectParentNode = function () {
            var selection = this.selection;
            if (selection) {
                var parent = selection.parent;
                if (parent) {
                    this.updateSelectionNode(parent);
                }
            }
        };
        Workspace.prototype.selectionViewConfigKey = function () {
            return this.selectionConfigKey("view/");
        };
        Workspace.prototype.selectionConfigKey = function (prefix) {
            if (prefix === void 0) { prefix = ""; }
            var key = null;
            var selection = this.selection;
            if (selection) {
                key = prefix + selection.domain;
                var typeName = selection.typeName;
                if (!typeName) {
                    typeName = selection.title;
                }
                key += "/" + typeName;
                if (selection.isFolder()) {
                    key += "/folder";
                }
            }
            return key;
        };
        Workspace.prototype.moveIfViewInvalid = function () {
            var workspace = this;
            var uri = Core.trimLeading(this.$location.path(), "/");
            if (this.selection) {
                var key = this.selectionViewConfigKey();
                if (this.validSelection(uri)) {
                    this.setLocalStorage(key, uri);
                    return false;
                }
                else {
                    log.info("the uri '" + uri + "' is not valid for this selection");
                    var defaultPath = this.getLocalStorage(key);
                    if (!defaultPath || !this.validSelection(defaultPath)) {
                        defaultPath = null;
                        angular.forEach(this.subLevelTabs, function (tab) {
                            var fn = tab.isValid;
                            if (!defaultPath && tab.href && angular.isDefined(fn) && fn(workspace)) {
                                defaultPath = tab.href();
                            }
                        });
                    }
                    if (!defaultPath) {
                        defaultPath = "#/jmx/help";
                    }
                    log.info("moving the URL to be " + defaultPath);
                    if (defaultPath.startsWith("#")) {
                        defaultPath = defaultPath.substring(1);
                    }
                    this.$location.path(defaultPath);
                    return true;
                }
            }
            else {
                return false;
            }
        };
        Workspace.prototype.updateSelectionNode = function (node) {
            var originalSelection = this.selection;
            this.selection = node;
            var key = null;
            if (node) {
                key = node['key'];
            }
            var $location = this.$location;
            var q = $location.search();
            if (key) {
                q['nid'] = key;
            }
            $location.search(q);
            if (originalSelection) {
                key = this.selectionViewConfigKey();
                if (key) {
                    var defaultPath = this.getLocalStorage(key);
                    if (defaultPath) {
                        this.$location.path(defaultPath);
                    }
                }
            }
        };
        Workspace.prototype.redrawTree = function () {
            var treeElement = this.treeElement;
            if (treeElement && angular.isDefined(treeElement.dynatree) && angular.isFunction(treeElement.dynatree)) {
                var node = treeElement.dynatree("getTree");
                if (angular.isDefined(node)) {
                    try {
                        node.reload();
                    }
                    catch (e) {
                    }
                }
            }
        };
        Workspace.prototype.expandSelection = function (flag) {
            var treeElement = this.treeElement;
            if (treeElement && angular.isDefined(treeElement.dynatree) && angular.isFunction(treeElement.dynatree)) {
                var node = treeElement.dynatree("getActiveNode");
                if (angular.isDefined(node)) {
                    node.expand(flag);
                }
            }
        };
        Workspace.prototype.matchesProperties = function (entries, properties) {
            if (!entries)
                return false;
            for (var key in properties) {
                var value = properties[key];
                if (!value || entries[key] !== value) {
                    return false;
                }
            }
            return true;
        };
        Workspace.prototype.hasInvokeRightsForName = function (objectName) {
            var methods = [];
            for (var _i = 1; _i < arguments.length; _i++) {
                methods[_i - 1] = arguments[_i];
            }
            var canInvoke = true;
            if (objectName) {
                var mbean = Core.parseMBean(objectName);
                if (mbean) {
                    var mbeanFolder = this.findMBeanWithProperties(mbean.domain, mbean.attributes);
                    if (mbeanFolder) {
                        return this.hasInvokeRights.apply(this, [mbeanFolder].concat(methods));
                    }
                    else {
                        log.debug("Failed to find mbean folder with name " + objectName);
                    }
                }
                else {
                    log.debug("Failed to parse mbean name " + objectName);
                }
            }
            return canInvoke;
        };
        Workspace.prototype.hasInvokeRights = function (selection) {
            var methods = [];
            for (var _i = 1; _i < arguments.length; _i++) {
                methods[_i - 1] = arguments[_i];
            }
            var canInvoke = true;
            if (selection) {
                var selectionFolder = selection;
                var mbean = selectionFolder.mbean;
                if (mbean) {
                    if (angular.isDefined(mbean.canInvoke)) {
                        canInvoke = mbean.canInvoke;
                    }
                    if (canInvoke && methods && methods.length > 0) {
                        var opsByString = mbean['opByString'];
                        var ops = mbean['op'];
                        if (opsByString && ops) {
                            methods.forEach(function (method) {
                                if (!canInvoke) {
                                    return;
                                }
                                var op = null;
                                if (method.endsWith(')')) {
                                    op = opsByString[method];
                                }
                                else {
                                    op = ops[method];
                                }
                                if (!op) {
                                    log.debug("Could not find method:", method, " to check permissions, skipping");
                                    return;
                                }
                                if (angular.isDefined(op.canInvoke)) {
                                    canInvoke = op.canInvoke;
                                }
                            });
                        }
                    }
                }
            }
            return canInvoke;
        };
        Workspace.prototype.treeContainsDomainAndProperties = function (domainName, properties) {
            var _this = this;
            if (properties === void 0) { properties = null; }
            var workspace = this;
            var tree = workspace.tree;
            if (tree) {
                var folder = tree.get(domainName);
                if (folder) {
                    if (properties) {
                        var children = folder.children || [];
                        var checkProperties = function (node) {
                            if (!_this.matchesProperties(node.entries, properties)) {
                                if (node.domain === domainName && node.children && node.children.length > 0) {
                                    return node.children.some(checkProperties);
                                }
                                else {
                                    return false;
                                }
                            }
                            else {
                                return true;
                            }
                        };
                        return children.some(checkProperties);
                    }
                    return true;
                }
                else {
                }
            }
            else {
            }
            return false;
        };
        Workspace.prototype.matches = function (folder, properties, propertiesCount) {
            if (folder) {
                var entries = folder.entries;
                if (properties) {
                    if (!entries)
                        return false;
                    for (var key in properties) {
                        var value = properties[key];
                        if (!value || entries[key] !== value) {
                            return false;
                        }
                    }
                }
                if (propertiesCount) {
                    return entries && Object.keys(entries).length === propertiesCount;
                }
                return true;
            }
            return false;
        };
        Workspace.prototype.hasDomainAndProperties = function (domainName, properties, propertiesCount) {
            if (properties === void 0) { properties = null; }
            if (propertiesCount === void 0) { propertiesCount = null; }
            var node = this.selection;
            if (node) {
                return this.matches(node, properties, propertiesCount) && node.domain === domainName;
            }
            return false;
        };
        Workspace.prototype.findMBeanWithProperties = function (domainName, properties, propertiesCount) {
            if (properties === void 0) { properties = null; }
            if (propertiesCount === void 0) { propertiesCount = null; }
            var tree = this.tree;
            if (tree) {
                return this.findChildMBeanWithProperties(tree.get(domainName), properties, propertiesCount);
            }
            return null;
        };
        Workspace.prototype.findChildMBeanWithProperties = function (folder, properties, propertiesCount) {
            var _this = this;
            if (properties === void 0) { properties = null; }
            if (propertiesCount === void 0) { propertiesCount = null; }
            var workspace = this;
            if (folder) {
                var children = folder.children;
                if (children) {
                    var answer = children.find(function (node) { return _this.matches(node, properties, propertiesCount); });
                    if (answer) {
                        return answer;
                    }
                    return children.map(function (node) { return workspace.findChildMBeanWithProperties(node, properties, propertiesCount); }).find(function (node) { return node; });
                }
            }
            return null;
        };
        Workspace.prototype.selectionHasDomainAndLastFolderName = function (objectName, lastName) {
            var lastNameLower = (lastName || "").toLowerCase();
            function isName(name) {
                return (name || "").toLowerCase() === lastNameLower;
            }
            var node = this.selection;
            if (node) {
                if (objectName === node.domain) {
                    var folders = node.folderNames;
                    if (folders) {
                        var last = folders.last();
                        return (isName(last) || isName(node.title)) && node.isFolder() && !node.objectName;
                    }
                }
            }
            return false;
        };
        Workspace.prototype.selectionHasDomain = function (domainName) {
            var node = this.selection;
            if (node) {
                return domainName === node.domain;
            }
            return false;
        };
        Workspace.prototype.selectionHasDomainAndType = function (objectName, typeName) {
            var node = this.selection;
            if (node) {
                return objectName === node.domain && typeName === node.typeName;
            }
            return false;
        };
        Workspace.prototype.hasMBeans = function () {
            var answer = false;
            var tree = this.tree;
            if (tree) {
                var children = tree.children;
                if (angular.isArray(children) && children.length > 0) {
                    answer = true;
                }
            }
            return answer;
        };
        Workspace.prototype.hasFabricMBean = function () {
            return this.hasDomainAndProperties('io.fabric8', { type: 'Fabric' });
        };
        Workspace.prototype.isFabricFolder = function () {
            return this.hasDomainAndProperties('io.fabric8');
        };
        Workspace.prototype.isCamelContext = function () {
            return this.hasDomainAndProperties('org.apache.camel', { type: 'context' });
        };
        Workspace.prototype.isCamelFolder = function () {
            return this.hasDomainAndProperties('org.apache.camel');
        };
        Workspace.prototype.isEndpointsFolder = function () {
            return this.selectionHasDomainAndLastFolderName('org.apache.camel', 'endpoints');
        };
        Workspace.prototype.isEndpoint = function () {
            return this.hasDomainAndProperties('org.apache.camel', { type: 'endpoints' });
        };
        Workspace.prototype.isRoutesFolder = function () {
            return this.selectionHasDomainAndLastFolderName('org.apache.camel', 'routes');
        };
        Workspace.prototype.isRoute = function () {
            return this.hasDomainAndProperties('org.apache.camel', { type: 'routes' });
        };
        Workspace.prototype.isOsgiFolder = function () {
            return this.hasDomainAndProperties('osgi.core');
        };
        Workspace.prototype.isKarafFolder = function () {
            return this.hasDomainAndProperties('org.apache.karaf');
        };
        Workspace.prototype.isOsgiCompendiumFolder = function () {
            return this.hasDomainAndProperties('osgi.compendium');
        };
        return Workspace;
    })();
    Core.Workspace = Workspace;
})(Core || (Core = {}));
var Workspace = (function (_super) {
    __extends(Workspace, _super);
    function Workspace() {
        _super.apply(this, arguments);
    }
    return Workspace;
})(Core.Workspace);
;
var UI;
(function (UI) {
    UI.colors = ["#5484ED", "#A4BDFC", "#46D6DB", "#7AE7BF", "#51B749", "#FBD75B", "#FFB878", "#FF887C", "#DC2127", "#DBADFF", "#E1E1E1"];
})(UI || (UI = {}));
var Core;
(function (Core) {
    Core.log = Logger.get("Core");
    Core.lazyLoaders = {};
})(Core || (Core = {}));
var numberTypeNames = {
    'byte': true,
    'short': true,
    'int': true,
    'long': true,
    'float': true,
    'double': true,
    'java.lang.byte': true,
    'java.lang.short': true,
    'java.lang.integer': true,
    'java.lang.long': true,
    'java.lang.float': true,
    'java.lang.double': true
};
function lineCount(value) {
    var rows = 0;
    if (value) {
        rows = 1;
        value.toString().each(/\n/, function () { return rows++; });
    }
    return rows;
}
function safeNull(value) {
    if (typeof value === 'boolean') {
        return value;
    }
    else if (typeof value === 'number') {
        return value;
    }
    if (value) {
        return value;
    }
    else {
        return "";
    }
}
function safeNullAsString(value, type) {
    if (typeof value === 'boolean') {
        return "" + value;
    }
    else if (typeof value === 'number') {
        return "" + value;
    }
    else if (typeof value === 'string') {
        return "" + value;
    }
    else if (type === 'javax.management.openmbean.CompositeData' || type === '[Ljavax.management.openmbean.CompositeData;' || type === 'java.util.Map') {
        var data = angular.toJson(value, true);
        return data;
    }
    else if (type === 'javax.management.ObjectName') {
        return "" + (value == null ? "" : value.canonicalName);
    }
    else if (type === 'javax.management.openmbean.TabularData') {
        var arr = [];
        for (var key in value) {
            var val = value[key];
            var line = "" + key + "=" + val;
            arr.push(line);
        }
        arr = arr.sortBy(function (row) { return row.toString(); });
        return arr.join("\n");
    }
    else if (angular.isArray(value)) {
        return value.join("\n");
    }
    else if (value) {
        return "" + value;
    }
    else {
        return "";
    }
}
function toSearchArgumentArray(value) {
    if (value) {
        if (angular.isArray(value))
            return value;
        if (angular.isString(value))
            return value.split(',');
    }
    return [];
}
function folderMatchesPatterns(node, patterns) {
    if (node) {
        var folderNames = node.folderNames;
        if (folderNames) {
            return patterns.any(function (ignorePaths) {
                for (var i = 0; i < ignorePaths.length; i++) {
                    var folderName = folderNames[i];
                    var ignorePath = ignorePaths[i];
                    if (!folderName)
                        return false;
                    var idx = ignorePath.indexOf(folderName);
                    if (idx < 0) {
                        return false;
                    }
                }
                return true;
            });
        }
    }
    return false;
}
function scopeStoreJolokiaHandle($scope, jolokia, jolokiaHandle) {
    if (jolokiaHandle) {
        $scope.$on('$destroy', function () {
            closeHandle($scope, jolokia);
        });
        $scope.jolokiaHandle = jolokiaHandle;
    }
}
function closeHandle($scope, jolokia) {
    var jolokiaHandle = $scope.jolokiaHandle;
    if (jolokiaHandle) {
        jolokia.unregister(jolokiaHandle);
        $scope.jolokiaHandle = null;
    }
}
function onSuccess(fn, options) {
    if (options === void 0) { options = {}; }
    options['mimeType'] = 'application/json';
    if (angular.isDefined(fn)) {
        options['success'] = fn;
    }
    if (!options['method']) {
        options['method'] = "POST";
    }
    options['canonicalNaming'] = false;
    options['canonicalProperties'] = false;
    if (!options['error']) {
        options['error'] = function (response) {
            Core.defaultJolokiaErrorHandler(response, options);
        };
    }
    return options;
}
function supportsLocalStorage() {
    try {
        return 'localStorage' in window && window['localStorage'] !== null;
    }
    catch (e) {
        return false;
    }
}
function isNumberTypeName(typeName) {
    if (typeName) {
        var text = typeName.toString().toLowerCase();
        var flag = numberTypeNames[text];
        return flag;
    }
    return false;
}
function encodeMBeanPath(mbean) {
    return mbean.replace(/\//g, '!/').replace(':', '/').escapeURL();
}
function escapeMBeanPath(mbean) {
    return mbean.replace(/\//g, '!/').replace(':', '/');
}
function encodeMBean(mbean) {
    return mbean.replace(/\//g, '!/').escapeURL();
}
function escapeDots(text) {
    return text.replace(/\./g, '-');
}
function escapeTreeCssStyles(text) {
    return escapeDots(text).replace(/span/g, 'sp-an');
}
function showLogPanel() {
    var log = $("#log-panel");
    var body = $('body');
    localStorage['showLog'] = 'true';
    log.css({ 'bottom': '50%' });
    body.css({
        'overflow-y': 'hidden'
    });
}
function logLevelClass(level) {
    if (level) {
        var first = level[0];
        if (first === 'w' || first === "W") {
            return "warning";
        }
        else if (first === 'e' || first === "E") {
            return "error";
        }
        else if (first === 'i' || first === "I") {
            return "info";
        }
        else if (first === 'd' || first === "D") {
            return "";
        }
    }
    return "";
}
var Core;
(function (Core) {
    function toPath(hashUrl) {
        if (Core.isBlank(hashUrl)) {
            return hashUrl;
        }
        if (hashUrl.startsWith("#")) {
            return hashUrl.substring(1);
        }
        else {
            return hashUrl;
        }
    }
    Core.toPath = toPath;
    function parseMBean(mbean) {
        var answer = {};
        var parts = mbean.split(":");
        if (parts.length > 1) {
            answer['domain'] = parts.first();
            parts = parts.exclude(parts.first());
            parts = parts.join(":");
            answer['attributes'] = {};
            var nameValues = parts.split(",");
            nameValues.forEach(function (str) {
                var nameValue = str.split('=');
                var name = nameValue.first().trim();
                nameValue = nameValue.exclude(nameValue.first());
                answer['attributes'][name] = nameValue.join('=').trim();
            });
        }
        return answer;
    }
    Core.parseMBean = parseMBean;
    function executePostLoginTasks() {
        Core.log.debug("Executing post login tasks");
        Core.postLoginTasks.execute();
    }
    Core.executePostLoginTasks = executePostLoginTasks;
    function executePreLogoutTasks(onComplete) {
        Core.log.debug("Executing pre logout tasks");
        Core.preLogoutTasks.onComplete(onComplete);
        Core.preLogoutTasks.execute();
    }
    Core.executePreLogoutTasks = executePreLogoutTasks;
    function logout(jolokiaUrl, userDetails, localStorage, $scope, successCB, errorCB) {
        if (successCB === void 0) { successCB = null; }
        if (errorCB === void 0) { errorCB = null; }
        if (jolokiaUrl) {
            var url = "auth/logout/";
            Core.executePreLogoutTasks(function () {
                $.ajax(url, {
                    type: "POST",
                    success: function () {
                        userDetails.username = null;
                        userDetails.password = null;
                        userDetails.loginDetails = null;
                        userDetails.rememberMe = false;
                        delete localStorage['userDetails'];
                        var jvmConnect = angular.fromJson(localStorage['jvmConnect']);
                        _.each(jvmConnect, function (value) {
                            delete value['userName'];
                            delete value['password'];
                        });
                        localStorage.setItem('jvmConnect', angular.toJson(jvmConnect));
                        localStorage.removeItem('activemqUserName');
                        localStorage.removeItem('activemqPassword');
                        if (successCB && angular.isFunction(successCB)) {
                            successCB();
                        }
                        Core.$apply($scope);
                    },
                    error: function (xhr, textStatus, error) {
                        userDetails.username = null;
                        userDetails.password = null;
                        userDetails.loginDetails = null;
                        userDetails.rememberMe = false;
                        delete localStorage['userDetails'];
                        var jvmConnect = angular.fromJson(localStorage['jvmConnect']);
                        _.each(jvmConnect, function (value) {
                            delete value['userName'];
                            delete value['password'];
                        });
                        localStorage.setItem('jvmConnect', angular.toJson(jvmConnect));
                        localStorage.removeItem('activemqUserName');
                        localStorage.removeItem('activemqPassword');
                        switch (xhr.status) {
                            case 401:
                                Core.log.debug('Failed to log out, ', error);
                                break;
                            case 403:
                                Core.log.debug('Failed to log out, ', error);
                                break;
                            case 0:
                                break;
                            default:
                                Core.log.debug('Failed to log out, ', error);
                                break;
                        }
                        if (errorCB && angular.isFunction(errorCB)) {
                            errorCB();
                        }
                        Core.$apply($scope);
                    }
                });
            });
        }
    }
    Core.logout = logout;
    function createHref($location, href, removeParams) {
        if (removeParams === void 0) { removeParams = null; }
        var hashMap = angular.copy($location.search());
        if (removeParams) {
            angular.forEach(removeParams, function (param) { return delete hashMap[param]; });
        }
        var hash = Core.hashToString(hashMap);
        if (hash) {
            var prefix = (href.indexOf("?") >= 0) ? "&" : "?";
            href += prefix + hash;
        }
        return href;
    }
    Core.createHref = createHref;
    function hashToString(hash) {
        var keyValuePairs = [];
        angular.forEach(hash, function (value, key) {
            keyValuePairs.push(key + "=" + value);
        });
        var params = keyValuePairs.join("&");
        return encodeURI(params);
    }
    Core.hashToString = hashToString;
    function stringToHash(hashAsString) {
        var entries = {};
        if (hashAsString) {
            var text = decodeURI(hashAsString);
            var items = text.split('&');
            angular.forEach(items, function (item) {
                var kv = item.split('=');
                var key = kv[0];
                var value = kv[1] || key;
                entries[key] = value;
            });
        }
        return entries;
    }
    Core.stringToHash = stringToHash;
    function registerForChanges(jolokia, $scope, arguments, callback, options) {
        var decorated = {
            responseJson: '',
            success: function (response) {
                var json = angular.toJson(response.value);
                if (decorated.responseJson !== json) {
                    decorated.responseJson = json;
                    callback(response);
                }
            }
        };
        angular.extend(decorated, options);
        return Core.register(jolokia, $scope, arguments, onSuccess(undefined, decorated));
    }
    Core.registerForChanges = registerForChanges;
    var responseHistory = null;
    function getOrInitObjectFromLocalStorage(key) {
        var answer = undefined;
        if (!(key in localStorage)) {
            localStorage[key] = angular.toJson({});
        }
        return angular.fromJson(localStorage[key]);
    }
    Core.getOrInitObjectFromLocalStorage = getOrInitObjectFromLocalStorage;
    function argumentsToString(arguments) {
        return StringHelpers.toString(arguments);
    }
    function keyForArgument(argument) {
        if (!('type' in argument)) {
            return null;
        }
        var answer = argument['type'];
        switch (answer.toLowerCase()) {
            case 'exec':
                answer += ':' + argument['mbean'] + ':' + argument['operation'];
                var argString = argumentsToString(argument['arguments']);
                if (!Core.isBlank(argString)) {
                    answer += ':' + argString;
                }
                break;
            case 'read':
                answer += ':' + argument['mbean'] + ':' + argument['attribute'];
                break;
            default:
                return null;
        }
        return answer;
    }
    function createResponseKey(arguments) {
        var answer = '';
        if (angular.isArray(arguments)) {
            answer = arguments.map(function (arg) {
                return keyForArgument(arg);
            }).join(':');
        }
        else {
            answer = keyForArgument(arguments);
        }
        return answer;
    }
    function getResponseHistory() {
        if (responseHistory === null) {
            responseHistory = {};
            Core.log.debug("Created response history", responseHistory);
        }
        return responseHistory;
    }
    Core.getResponseHistory = getResponseHistory;
    Core.MAX_RESPONSE_CACHE_SIZE = 20;
    function getOldestKey(responseHistory) {
        var oldest = null;
        var oldestKey = null;
        angular.forEach(responseHistory, function (value, key) {
            if (!value || !value.timestamp) {
                oldest = 0;
                oldestKey = key;
            }
            else if (oldest === null || value.timestamp < oldest) {
                oldest = value.timestamp;
                oldestKey = key;
            }
        });
        return oldestKey;
    }
    function addResponse(arguments, value) {
        var responseHistory = getResponseHistory();
        var key = createResponseKey(arguments);
        if (key === null) {
            Core.log.debug("key for arguments is null, not caching: ", StringHelpers.toString(arguments));
            return;
        }
        var keys = Object.extended(responseHistory).keys();
        if (keys.length >= Core.MAX_RESPONSE_CACHE_SIZE) {
            Core.log.debug("Cache limit (", Core.MAX_RESPONSE_CACHE_SIZE, ") met or  exceeded (", keys.length, "), trimming oldest response");
            var oldestKey = getOldestKey(responseHistory);
            if (oldestKey !== null) {
                Core.log.debug("Deleting key: ", oldestKey);
                delete responseHistory[oldestKey];
            }
            else {
                Core.log.debug("Got null key, could be a cache problem, wiping cache");
                keys.forEach(function (key) {
                    Core.log.debug("Deleting key: ", key);
                    delete responseHistory[key];
                });
            }
        }
        responseHistory[key] = value;
    }
    function getResponse(jolokia, arguments, callback) {
        var responseHistory = getResponseHistory();
        var key = createResponseKey(arguments);
        if (key === null) {
            jolokia.request(arguments, callback);
            return;
        }
        if (key in responseHistory && 'success' in callback) {
            var value = responseHistory[key];
            setTimeout(function () {
                callback['success'](value);
            }, 10);
        }
        else {
            Core.log.debug("Unable to find existing response for key: ", key);
            jolokia.request(arguments, callback);
        }
    }
    function register(jolokia, scope, arguments, callback) {
        if (!angular.isDefined(scope.$jhandle) || !angular.isArray(scope.$jhandle)) {
            scope.$jhandle = [];
        }
        else {
        }
        if (angular.isDefined(scope.$on)) {
            scope.$on('$destroy', function (event) {
                unregister(jolokia, scope);
            });
        }
        var handle = null;
        if ('success' in callback) {
            var cb = callback.success;
            var args = arguments;
            callback.success = function (response) {
                addResponse(args, response);
                cb(response);
            };
        }
        if (angular.isArray(arguments)) {
            if (arguments.length >= 1) {
                var args = [callback];
                angular.forEach(arguments, function (value) { return args.push(value); });
                var registerFn = jolokia.register;
                handle = registerFn.apply(jolokia, args);
                scope.$jhandle.push(handle);
                getResponse(jolokia, arguments, callback);
            }
        }
        else {
            handle = jolokia.register(callback, arguments);
            scope.$jhandle.push(handle);
            getResponse(jolokia, arguments, callback);
        }
        return function () {
            if (handle !== null) {
                scope.$jhandle.remove(handle);
                jolokia.unregister(handle);
            }
        };
    }
    Core.register = register;
    function unregister(jolokia, scope) {
        if (angular.isDefined(scope.$jhandle)) {
            scope.$jhandle.forEach(function (handle) {
                jolokia.unregister(handle);
            });
            delete scope.$jhandle;
        }
    }
    Core.unregister = unregister;
    function defaultJolokiaErrorHandler(response, options) {
        if (options === void 0) { options = {}; }
        var stacktrace = response.stacktrace;
        if (stacktrace) {
            var silent = options['silent'];
            if (!silent) {
                var operation = Core.pathGet(response, ['request', 'operation']) || "unknown";
                if (stacktrace.indexOf("javax.management.InstanceNotFoundException") >= 0 || stacktrace.indexOf("javax.management.AttributeNotFoundException") >= 0 || stacktrace.indexOf("java.lang.IllegalArgumentException: No operation") >= 0) {
                    Core.log.debug("Operation ", operation, " failed due to: ", response['error']);
                }
                else {
                    Core.log.warn("Operation ", operation, " failed due to: ", response['error']);
                }
            }
            else {
                Core.log.debug("Operation ", operation, " failed due to: ", response['error']);
            }
        }
    }
    Core.defaultJolokiaErrorHandler = defaultJolokiaErrorHandler;
    function logJolokiaStackTrace(response) {
        var stacktrace = response.stacktrace;
        if (stacktrace) {
            var operation = Core.pathGet(response, ['request', 'operation']) || "unknown";
            Core.log.info("Operation ", operation, " failed due to: ", response['error']);
        }
    }
    Core.logJolokiaStackTrace = logJolokiaStackTrace;
    function xmlNodeToString(xmlNode) {
        try {
            return (new XMLSerializer()).serializeToString(xmlNode);
        }
        catch (e) {
            try {
                return xmlNode.xml;
            }
            catch (e) {
                console.log('WARNING: XMLSerializer not supported');
            }
        }
        return false;
    }
    Core.xmlNodeToString = xmlNodeToString;
    function isTextNode(node) {
        return node && node.nodeType === 3;
    }
    Core.isTextNode = isTextNode;
    function fileExtension(name, defaultValue) {
        if (defaultValue === void 0) { defaultValue = ""; }
        var extension = defaultValue;
        if (name) {
            var idx = name.lastIndexOf(".");
            if (idx > 0) {
                extension = name.substring(idx + 1, name.length).toLowerCase();
            }
        }
        return extension;
    }
    Core.fileExtension = fileExtension;
    function getUUID() {
        var d = new Date();
        var ms = (d.getTime() * 1000) + d.getUTCMilliseconds();
        var random = Math.floor((1 + Math.random()) * 0x10000);
        return ms.toString(16) + random.toString(16);
    }
    Core.getUUID = getUUID;
    var _versionRegex = /[^\d]*(\d+)\.(\d+)(\.(\d+))?.*/;
    function parseVersionNumbers(text) {
        if (text) {
            var m = text.match(_versionRegex);
            if (m && m.length > 4) {
                var m1 = m[1];
                var m2 = m[2];
                var m4 = m[4];
                if (angular.isDefined(m4)) {
                    return [parseInt(m1), parseInt(m2), parseInt(m4)];
                }
                else if (angular.isDefined(m2)) {
                    return [parseInt(m1), parseInt(m2)];
                }
                else if (angular.isDefined(m1)) {
                    return [parseInt(m1)];
                }
            }
        }
        return null;
    }
    Core.parseVersionNumbers = parseVersionNumbers;
    function versionToSortableString(version, maxDigitsBetweenDots) {
        if (maxDigitsBetweenDots === void 0) { maxDigitsBetweenDots = 4; }
        return (version || "").split(".").map(function (x) {
            var length = x.length;
            return (length >= maxDigitsBetweenDots) ? x : x.padLeft(' ', maxDigitsBetweenDots - length);
        }).join(".");
    }
    Core.versionToSortableString = versionToSortableString;
    function time(message, fn) {
        var start = new Date().getTime();
        var answer = fn();
        var elapsed = new Date().getTime() - start;
        console.log(message + " " + elapsed);
        return answer;
    }
    Core.time = time;
    function compareVersionNumberArrays(v1, v2) {
        if (v1 && !v2) {
            return 1;
        }
        if (!v1 && v2) {
            return -1;
        }
        if (v1 === v2) {
            return 0;
        }
        for (var i = 0; i < v1.length; i++) {
            var n1 = v1[i];
            if (i >= v2.length) {
                return 1;
            }
            var n2 = v2[i];
            if (!angular.isDefined(n1)) {
                return -1;
            }
            if (!angular.isDefined(n2)) {
                return 1;
            }
            if (n1 > n2) {
                return 1;
            }
            else if (n1 < n2) {
                return -1;
            }
        }
        return 0;
    }
    Core.compareVersionNumberArrays = compareVersionNumberArrays;
    function valueToHtml(value) {
        if (angular.isArray(value)) {
            var size = value.length;
            if (!size) {
                return "";
            }
            else if (size === 1) {
                return valueToHtml(value[0]);
            }
            else {
                var buffer = "<ul>";
                angular.forEach(value, function (childValue) {
                    buffer += "<li>" + valueToHtml(childValue) + "</li>";
                });
                return buffer + "</ul>";
            }
        }
        else if (angular.isObject(value)) {
            var buffer = "<table>";
            angular.forEach(value, function (childValue, key) {
                buffer += "<tr><td>" + key + "</td><td>" + valueToHtml(childValue) + "</td></tr>";
            });
            return buffer + "</table>";
        }
        else if (angular.isString(value)) {
            var uriPrefixes = ["http://", "https://", "file://", "mailto:"];
            var answer = value;
            angular.forEach(uriPrefixes, function (prefix) {
                if (answer.startsWith(prefix)) {
                    answer = "<a href='" + value + "'>" + value + "</a>";
                }
            });
            return answer;
        }
        return value;
    }
    Core.valueToHtml = valueToHtml;
    function tryParseJson(text) {
        text = text.trim();
        if ((text.startsWith("[") && text.endsWith("]")) || (text.startsWith("{") && text.endsWith("}"))) {
            try {
                return JSON.parse(text);
            }
            catch (e) {
            }
        }
        return null;
    }
    Core.tryParseJson = tryParseJson;
    function maybePlural(count, word) {
        var pluralWord = (count === 1) ? word : word.pluralize();
        return "" + count + " " + pluralWord;
    }
    Core.maybePlural = maybePlural;
    function objectNameProperties(objectName) {
        var entries = {};
        if (objectName) {
            var idx = objectName.indexOf(":");
            if (idx > 0) {
                var path = objectName.substring(idx + 1);
                var items = path.split(',');
                angular.forEach(items, function (item) {
                    var kv = item.split('=');
                    var key = kv[0];
                    var value = kv[1] || key;
                    entries[key] = value;
                });
            }
        }
        return entries;
    }
    Core.objectNameProperties = objectNameProperties;
    function setPageTitle($document, title) {
        $document.attr('title', title.getTitleWithSeparator(' '));
    }
    Core.setPageTitle = setPageTitle;
    function setPageTitleWithTab($document, title, tab) {
        $document.attr('title', title.getTitleWithSeparator(' ') + " " + tab);
    }
    Core.setPageTitleWithTab = setPageTitleWithTab;
    function getMBeanTypeFolder(workspace, domain, typeName) {
        if (workspace) {
            var mbeanTypesToDomain = workspace.mbeanTypesToDomain || {};
            var types = mbeanTypesToDomain[typeName] || {};
            var answer = types[domain];
            if (angular.isArray(answer) && answer.length) {
                return answer[0];
            }
            return answer;
        }
        return null;
    }
    Core.getMBeanTypeFolder = getMBeanTypeFolder;
    function getMBeanTypeObjectName(workspace, domain, typeName) {
        var folder = Core.getMBeanTypeFolder(workspace, domain, typeName);
        return Core.pathGet(folder, ["objectName"]);
    }
    Core.getMBeanTypeObjectName = getMBeanTypeObjectName;
    function toSafeDomID(text) {
        return text ? text.replace(/(\/|\.)/g, "_") : text;
    }
    Core.toSafeDomID = toSafeDomID;
    function forEachLeafFolder(folders, fn) {
        angular.forEach(folders, function (folder) {
            var children = folder["children"];
            if (angular.isArray(children) && children.length > 0) {
                forEachLeafFolder(children, fn);
            }
            else {
                fn(folder);
            }
        });
    }
    Core.forEachLeafFolder = forEachLeafFolder;
    function extractHashURL(url) {
        var parts = url.split('#');
        if (parts.length === 0) {
            return url;
        }
        var answer = parts[1];
        if (parts.length > 1) {
            var remaining = parts.last(parts.length - 2);
            remaining.forEach(function (part) {
                answer = answer + "#" + part;
            });
        }
        return answer;
    }
    Core.extractHashURL = extractHashURL;
    function authHeaderValue(userDetails) {
        return getBasicAuthHeader(userDetails.username, userDetails.password);
    }
    Core.authHeaderValue = authHeaderValue;
    function getBasicAuthHeader(username, password) {
        var authInfo = username + ":" + password;
        authInfo = authInfo.encodeBase64();
        return "Basic " + authInfo;
    }
    Core.getBasicAuthHeader = getBasicAuthHeader;
    var httpRegex = new RegExp('^(https?):\/\/(([^:/?#]*)(?::([0-9]+))?)');
    function parseUrl(url) {
        if (Core.isBlank(url)) {
            return null;
        }
        var matches = url.match(httpRegex);
        if (matches === null) {
            return null;
        }
        var scheme = matches[1];
        var host = matches[3];
        var port = matches[4];
        var parts = null;
        if (!Core.isBlank(port)) {
            parts = url.split(port);
        }
        else {
            parts = url.split(host);
        }
        var path = parts[1];
        if (path && path.startsWith('/')) {
            path = path.slice(1, path.length);
        }
        return {
            scheme: scheme,
            host: host,
            port: port,
            path: path
        };
    }
    Core.parseUrl = parseUrl;
    function getDocHeight() {
        var D = document;
        return Math.max(Math.max(D.body.scrollHeight, D.documentElement.scrollHeight), Math.max(D.body.offsetHeight, D.documentElement.offsetHeight), Math.max(D.body.clientHeight, D.documentElement.clientHeight));
    }
    Core.getDocHeight = getDocHeight;
    function useProxyIfExternal(connectUrl) {
        if (Core.isChromeApp()) {
            return connectUrl;
        }
        var host = window.location.host;
        if (!connectUrl.startsWith("http://" + host + "/") && !connectUrl.startsWith("https://" + host + "/")) {
            var idx = connectUrl.indexOf("://");
            if (idx > 0) {
                connectUrl = connectUrl.substring(idx + 3);
            }
            connectUrl = connectUrl.replace(":", "/");
            connectUrl = Core.trimLeading(connectUrl, "/");
            connectUrl = Core.trimTrailing(connectUrl, "/");
            connectUrl = Core.url("/proxy/" + connectUrl);
        }
        return connectUrl;
    }
    Core.useProxyIfExternal = useProxyIfExternal;
    function checkInjectorLoaded() {
        if (!Core.injector) {
            Core.injector = angular.element(document.documentElement).injector();
        }
    }
    Core.checkInjectorLoaded = checkInjectorLoaded;
    function getRecentConnections(localStorage) {
        if (Core.isBlank(localStorage['recentConnections'])) {
            Core.clearConnections();
        }
        return angular.fromJson(localStorage['recentConnections']);
    }
    Core.getRecentConnections = getRecentConnections;
    function addRecentConnection(localStorage, name) {
        var recent = getRecentConnections(localStorage);
        recent = recent.add(name).unique().first(5);
        localStorage['recentConnections'] = angular.toJson(recent);
    }
    Core.addRecentConnection = addRecentConnection;
    function removeRecentConnection(localStorage, name) {
        var recent = getRecentConnections(localStorage);
        recent = recent.exclude(function (n) {
            return n === name;
        });
        localStorage['recentConnections'] = angular.toJson(recent);
    }
    Core.removeRecentConnection = removeRecentConnection;
    function clearConnections() {
        localStorage['recentConnections'] = '[]';
    }
    Core.clearConnections = clearConnections;
    function saveConnection(options) {
        var connectionMap = Core.loadConnectionMap();
        var clone = Object.clone(options);
        delete clone.userName;
        delete clone.password;
        connectionMap[options.name] = clone;
        Core.saveConnectionMap(connectionMap);
    }
    Core.saveConnection = saveConnection;
    function connectToServer(localStorage, options) {
        Core.log.debug("Connecting with options: ", StringHelpers.toString(options));
        addRecentConnection(localStorage, options.name);
        if (!('userName' in options)) {
            var userDetails = Core.injector.get('userDetails');
            options.userName = userDetails.username;
            options.password = userDetails.password;
        }
        saveConnection(options);
        var $window = Core.injector.get('$window');
        var url = (options.view || '#/welcome') + '?con=' + options.name;
        url = url.replace(/\?/g, "&");
        url = url.replace(/&/, "?");
        var newWindow = $window.open(url);
        newWindow['con'] = options.name;
        $window['passUserDetails'] = {
            username: options.userName,
            password: options.password,
            loginDetails: {}
        };
    }
    Core.connectToServer = connectToServer;
    function extractTargetUrl($location, scheme, port) {
        if (angular.isUndefined(scheme)) {
            scheme = $location.scheme();
        }
        var host = $location.host();
        var qUrl = $location.absUrl();
        var idx = qUrl.indexOf("url=");
        if (idx > 0) {
            qUrl = qUrl.substr(idx + 4);
            var value = decodeURIComponent(qUrl);
            if (value) {
                idx = value.indexOf("/proxy/");
                if (idx > 0) {
                    value = value.substr(idx + 7);
                    idx = value.indexOf("://");
                    if (idx > 0) {
                        value = value.substr(idx + 3);
                    }
                    var data = value.split("/");
                    if (data.length >= 1) {
                        host = data[0];
                    }
                    if (angular.isUndefined(port) && data.length >= 2) {
                        var qPort = Core.parseIntValue(data[1], "port number");
                        if (qPort) {
                            port = qPort;
                        }
                    }
                }
            }
        }
        if (angular.isUndefined(port)) {
            port = $location.port();
        }
        var url = scheme + "://" + host;
        if (port != 80) {
            url += ":" + port;
        }
        return url;
    }
    Core.extractTargetUrl = extractTargetUrl;
    function isProxyUrl($location) {
        var url = $location.url();
        return url.indexOf('/hawtio/proxy/') > 0;
    }
    Core.isProxyUrl = isProxyUrl;
    function doNothing(value) {
        return value;
    }
    Core.doNothing = doNothing;
    Core.bindModelToSearchParam = ControllerHelpers.bindModelToSearchParam;
    Core.reloadWhenParametersChange = ControllerHelpers.reloadWhenParametersChange;
    function createJolokia(url, username, password) {
        var jolokiaParams = {
            url: url,
            username: username,
            password: password,
            canonicalNaming: false,
            ignoreErrors: true,
            mimeType: 'application/json'
        };
        return new Jolokia(jolokiaParams);
    }
    Core.createJolokia = createJolokia;
    function throttled(fn, millis) {
        var nextInvokeTime = 0;
        var lastAnswer = null;
        return function () {
            var now = Date.now();
            if (nextInvokeTime < now) {
                nextInvokeTime = now + millis;
                lastAnswer = fn();
            }
            else {
            }
            return lastAnswer;
        };
    }
    Core.throttled = throttled;
    function parseJsonText(text, message) {
        if (message === void 0) { message = "JSON"; }
        var answer = null;
        try {
            answer = angular.fromJson(text);
        }
        catch (e) {
            Core.log.info("Failed to parse " + message + " from: " + text + ". " + e);
        }
        return answer;
    }
    Core.parseJsonText = parseJsonText;
    function humanizeValueHtml(value) {
        var formattedValue = "";
        if (value === true) {
            formattedValue = '<i class="icon-check"></i>';
        }
        else if (value === false) {
            formattedValue = '<i class="icon-check-empty"></i>';
        }
        else {
            formattedValue = Core.humanizeValue(value);
        }
        return formattedValue;
    }
    Core.humanizeValueHtml = humanizeValueHtml;
    function getQueryParameterValue(url, parameterName) {
        var parts;
        var query = (url || '').split('?');
        if (query && query.length > 0) {
            parts = query[1];
        }
        else {
            parts = '';
        }
        var vars = parts.split('&');
        for (var i = 0; i < vars.length; i++) {
            var pair = vars[i].split('=');
            if (decodeURIComponent(pair[0]) == parameterName) {
                return decodeURIComponent(pair[1]);
            }
        }
        return null;
    }
    Core.getQueryParameterValue = getQueryParameterValue;
    function createRemoteWorkspace(remoteJolokia, $location, localStorage, $rootScope, $compile, $templateCache, userDetails) {
        if ($rootScope === void 0) { $rootScope = null; }
        if ($compile === void 0) { $compile = null; }
        if ($templateCache === void 0) { $templateCache = null; }
        if (userDetails === void 0) { userDetails = null; }
        var jolokiaStatus = {
            xhr: null
        };
        var jmxTreeLazyLoadRegistry = Core.lazyLoaders;
        var profileWorkspace = new Core.Workspace(remoteJolokia, jolokiaStatus, jmxTreeLazyLoadRegistry, $location, $compile, $templateCache, localStorage, $rootScope, userDetails);
        Core.log.info("Loading the profile using jolokia: " + remoteJolokia);
        profileWorkspace.loadTree();
        return profileWorkspace;
    }
    Core.createRemoteWorkspace = createRemoteWorkspace;
    function humanizeMilliseconds(value) {
        if (!angular.isNumber(value)) {
            return "XXX";
        }
        var seconds = value / 1000;
        var years = Math.floor(seconds / 31536000);
        if (years) {
            return maybePlural(years, "year");
        }
        var days = Math.floor((seconds %= 31536000) / 86400);
        if (days) {
            return maybePlural(days, "day");
        }
        var hours = Math.floor((seconds %= 86400) / 3600);
        if (hours) {
            return maybePlural(hours, 'hour');
        }
        var minutes = Math.floor((seconds %= 3600) / 60);
        if (minutes) {
            return maybePlural(minutes, 'minute');
        }
        seconds = Math.floor(seconds % 60);
        if (seconds) {
            return maybePlural(seconds, 'second');
        }
        return value + " ms";
    }
    Core.humanizeMilliseconds = humanizeMilliseconds;
    function storeConnectionRegex(regexs, name, json) {
        if (!regexs.any(function (r) {
            r['name'] === name;
        })) {
            var regex = '';
            if (json['useProxy']) {
                regex = '/hawtio/proxy/';
            }
            else {
                regex = '//';
            }
            regex += json['host'] + ':' + json['port'] + '/' + json['path'];
            regexs.push({
                name: name,
                regex: regex.escapeURL(true),
                color: UI.colors.sample()
            });
            writeRegexs(regexs);
        }
    }
    Core.storeConnectionRegex = storeConnectionRegex;
    function getRegexs() {
        var regexs = [];
        try {
            regexs = angular.fromJson(localStorage['regexs']);
        }
        catch (e) {
            delete localStorage['regexs'];
        }
        return regexs;
    }
    Core.getRegexs = getRegexs;
    function removeRegex(name) {
        var regexs = Core.getRegexs();
        var hasFunc = function (r) {
            return r['name'] === name;
        };
        if (regexs.any(hasFunc)) {
            regexs = regexs.exclude(hasFunc);
            Core.writeRegexs(regexs);
        }
    }
    Core.removeRegex = removeRegex;
    function writeRegexs(regexs) {
        localStorage['regexs'] = angular.toJson(regexs);
    }
    Core.writeRegexs = writeRegexs;
    function maskPassword(value) {
        if (value) {
            var text = '' + value;
            var userInfoPattern = "(.*://.*:)(.*)(@)";
            value = value.replace(new RegExp(userInfoPattern, 'i'), "$1xxxxxx$3");
        }
        return value;
    }
    Core.maskPassword = maskPassword;
    function matchFilterIgnoreCase(text, filter) {
        if (angular.isUndefined(text) || angular.isUndefined(filter)) {
            return true;
        }
        if (text == null || filter == null) {
            return true;
        }
        text = text.toString().trim().toLowerCase();
        filter = filter.toString().trim().toLowerCase();
        if (text.length === 0 || filter.length === 0) {
            return true;
        }
        var tokens = filter.split(",");
        tokens = tokens.filter(function (t) {
            return t.length > 0;
        }).map(function (t) {
            return t.trim();
        });
        var answer = tokens.some(function (t) {
            var bool = text.indexOf(t) > -1;
            return bool;
        });
        return answer;
    }
    Core.matchFilterIgnoreCase = matchFilterIgnoreCase;
})(Core || (Core = {}));
var IDE;
(function (IDE) {
    function getIdeMBean(workspace) {
        return Core.getMBeanTypeObjectName(workspace, "hawtio", "IdeFacade");
    }
    IDE.getIdeMBean = getIdeMBean;
    function isOpenInIdeaSupported(workspace, localStorage) {
        var value = localStorage["openInIDEA"];
        return value !== "false";
    }
    IDE.isOpenInIdeaSupported = isOpenInIdeaSupported;
    function isOpenInTextMateSupported(workspace, localStorage) {
        var value = localStorage["openInTextMate"];
        return value !== "false";
    }
    IDE.isOpenInTextMateSupported = isOpenInTextMateSupported;
    function findClassAbsoluteFileName(mbean, jolokia, localStorage, fileName, className, onResult) {
        var sourceRoots = [];
        var answer = null;
        if (mbean) {
            answer = jolokia.execute(mbean, "findClassAbsoluteFileName", fileName, className, sourceRoots, onSuccess(onResult));
        }
        else {
            onResult(answer);
        }
        return answer;
    }
    IDE.findClassAbsoluteFileName = findClassAbsoluteFileName;
    function asNumber(value, defaultValue) {
        if (defaultValue === void 0) { defaultValue = 0; }
        if (angular.isNumber(value)) {
            return value;
        }
        else if (angular.isString(value)) {
            return parseInt(value);
        }
        else {
            return defaultValue;
        }
    }
    function max(v1, v2) {
        return (v1 >= v2) ? v1 : v2;
    }
    function ideaOpenAndNavigate(mbean, jolokia, absoluteFileName, line, column, fn) {
        if (fn === void 0) { fn = null; }
        var answer = null;
        if (mbean) {
            line = max(asNumber(line) - 1, 0);
            column = max(asNumber(column) - 1, 0);
            answer = jolokia.execute(mbean, "ideaOpenAndNavigate", absoluteFileName, line, column, onSuccess(fn));
        }
        return answer;
    }
    IDE.ideaOpenAndNavigate = ideaOpenAndNavigate;
})(IDE || (IDE = {}));
var IDE;
(function (IDE) {
    var log = Logger.get("IDE");
    var OpenInIdeDirective = (function () {
        function OpenInIdeDirective(localStorage, workspace, jolokia) {
            var _this = this;
            this.localStorage = localStorage;
            this.workspace = workspace;
            this.jolokia = jolokia;
            this.restrict = 'E';
            this.replace = true;
            this.transclude = false;
            this.scope = {
                fileName: '@',
                className: '@',
                line: '@',
                column: '@'
            };
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        OpenInIdeDirective.prototype.doLink = function ($scope, $element, $attrs) {
            var workspace = this.workspace;
            var jolokia = this.jolokia;
            var mbean = IDE.getIdeMBean(workspace);
            var fileName = $scope.fileName;
            if (mbean && fileName) {
                var className = $scope.className;
                var line = $scope.line;
                var col = $scope.col;
                if (!angular.isDefined(line) || line === null)
                    line = 0;
                if (!angular.isDefined(col) || col === null)
                    col = 0;
                if (IDE.isOpenInIdeaSupported(workspace, localStorage)) {
                    var ideaButton = $('<button class="btn btn-mini"><img src="app/ide/img/intellijidea.png" width="16" height="16"></button>');
                    function onResult(absoluteName) {
                        if (!absoluteName) {
                            log.info("Could not find file in source code: " + fileName + " class: " + className);
                            ideaButton.attr("title", "Could not find source file: " + fileName);
                        }
                        else {
                            ideaButton.attr("title", "Opening in IDEA: " + absoluteName);
                            IDE.ideaOpenAndNavigate(mbean, jolokia, absoluteName, line, col);
                        }
                    }
                    ideaButton.on("click", function () {
                        log.info("Finding local file name: " + fileName + " className: " + className);
                        IDE.findClassAbsoluteFileName(mbean, jolokia, localStorage, fileName, className, onResult);
                    });
                    $element.append(ideaButton);
                }
            }
        };
        return OpenInIdeDirective;
    })();
    IDE.OpenInIdeDirective = OpenInIdeDirective;
})(IDE || (IDE = {}));
var IDE;
(function (IDE) {
    var pluginName = 'ide';
    IDE._module = angular.module(pluginName, ['bootstrap', 'hawtioCore']);
    IDE._module.directive('hawtioOpenIde', ["localStorage", "workspace", "jolokia", function (localStorage, workspace, jolokia) {
        return new IDE.OpenInIdeDirective(localStorage, workspace, jolokia);
    }]);
    IDE._module.run(["helpRegistry", function (helpRegistry) {
        helpRegistry.addDevDoc('IDE', 'app/ide/doc/developer.md');
    }]);
    hawtioPluginLoader.addModule(pluginName);
})(IDE || (IDE = {}));
var Core;
(function (Core) {
    var PageTitle = (function () {
        function PageTitle() {
            this.titleElements = [];
        }
        PageTitle.prototype.addTitleElement = function (element) {
            this.titleElements.push(element);
        };
        PageTitle.prototype.getTitle = function () {
            return this.getTitleExcluding([], ' ');
        };
        PageTitle.prototype.getTitleWithSeparator = function (separator) {
            return this.getTitleExcluding([], separator);
        };
        PageTitle.prototype.getTitleExcluding = function (excludes, separator) {
            return this.getTitleArrayExcluding(excludes).join(separator);
        };
        PageTitle.prototype.getTitleArrayExcluding = function (excludes) {
            return this.titleElements.map(function (element) {
                var answer = '';
                if (element) {
                    answer = element();
                    if (answer === null) {
                        return '';
                    }
                }
                return answer;
            }).exclude(excludes).exclude('');
        };
        return PageTitle;
    })();
    Core.PageTitle = PageTitle;
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core.pluginName = 'hawtioCore';
    Core.templatePath = 'app/core/html/';
    Core.jolokiaUrl = Core.getJolokiaUrl();
    Logger.get("Core").debug("jolokiaUrl " + Core.jolokiaUrl);
    Core._module = angular.module(Core.pluginName, ['bootstrap', 'ngResource', 'ui', 'ui.bootstrap.dialog', 'hawtio-ui']);
    Core._module.config(["$locationProvider", "$routeProvider", "$dialogProvider", function ($locationProvider, $routeProvider, $dialogProvider) {
        $locationProvider.html5Mode(true);
        $dialogProvider.options({
            backdropFade: true,
            dialogFade: true
        });
        $routeProvider.when('/help', {
            redirectTo: '/help/index'
        }).when('/login', { templateUrl: Core.templatePath + 'login.html' }).when('/welcome', { templateUrl: Core.templatePath + 'welcome.html' }).when('/about', { templateUrl: Core.templatePath + 'about.html' }).when('/help/:topic/', { templateUrl: Core.templatePath + 'help.html' }).when('/help/:topic/:subtopic', { templateUrl: Core.templatePath + 'help.html' });
    }]);
    Core._module.constant('layoutTree', Core.templatePath + 'layoutTree.html');
    Core._module.constant('layoutFull', Core.templatePath + 'layoutFull.html');
    Core._module.filter("valueToHtml", function () { return Core.valueToHtml; });
    Core._module.filter('humanize', function () { return Core.humanizeValue; });
    Core._module.filter('humanizeMs', function () { return Core.humanizeMilliseconds; });
    Core._module.filter('maskPassword', function () { return Core.maskPassword; });
    Core._module.run(["$rootScope", "$routeParams", "jolokia", "workspace", "localStorage", "viewRegistry", "layoutFull", "helpRegistry", "pageTitle", "branding", "toastr", "metricsWatcher", "userDetails", "preferencesRegistry", "postLoginTasks", "preLogoutTasks", "$location", "ConnectOptions", "locationChangeStartTasks", "$http", function ($rootScope, $routeParams, jolokia, workspace, localStorage, viewRegistry, layoutFull, helpRegistry, pageTitle, branding, toastr, metricsWatcher, userDetails, preferencesRegistry, postLoginTasks, preLogoutTasks, $location, ConnectOptions, locationChangeStartTasks, $http) {
        Core.checkInjectorLoaded();
        postLoginTasks.addTask("ResetPreLogoutTasks", function () {
            Core.checkInjectorLoaded();
            preLogoutTasks.reset();
        });
        preLogoutTasks.addTask("ResetPostLoginTasks", function () {
            Core.checkInjectorLoaded();
            postLoginTasks.reset();
        });
        $rootScope.lineCount = lineCount;
        $rootScope.params = $routeParams;
        $rootScope.is = function (type, value) {
            return angular['is' + type](value);
        };
        $rootScope.empty = function (value) {
            return $.isEmptyObject(value);
        };
        $rootScope.$on('UpdateRate', function (event, rate) {
            jolokia.stop();
            if (rate > 0) {
                jolokia.start(rate);
            }
            Logger.get("Core").debug("Set update rate to: ", rate);
        });
        $rootScope.$emit('UpdateRate', localStorage['updateRate']);
        $rootScope.$on('$locationChangeStart', function ($event, newUrl, oldUrl) {
            locationChangeStartTasks.execute($event, newUrl, oldUrl);
        });
        locationChangeStartTasks.addTask('ConParam', function ($event, newUrl, oldUrl) {
            if (!Core.injector) {
                return;
            }
            var $location = Core.injector.get('$location');
            var ConnectOptions = Core.injector.get('ConnectOptions');
            if (!ConnectOptions.name || !newUrl) {
                return;
            }
            var newQuery = $location.search();
            if (!newQuery.con) {
                Core.log.debug("Lost connection parameter (", ConnectOptions.name, ") from query params: ", newQuery, " resetting");
                newQuery['con'] = ConnectOptions.name;
                $location.search(newQuery);
            }
        });
        locationChangeStartTasks.addTask('UpdateSession', function () {
            Core.log.debug("Updating session expiry");
            $http({ method: 'post', url: 'refresh' }).success(function (data) {
                Core.log.debug("Updated session, response: ", data);
            }).error(function () {
                Core.log.debug("Failed to update session expiry");
            });
            Core.log.debug("Made request");
        });
        $rootScope.log = function (variable) {
            console.log(variable);
        };
        $rootScope.alert = function (text) {
            alert(text);
        };
        viewRegistry['fullscreen'] = layoutFull;
        viewRegistry['notree'] = layoutFull;
        viewRegistry['help'] = layoutFull;
        viewRegistry['welcome'] = layoutFull;
        viewRegistry['preferences'] = layoutFull;
        viewRegistry['about'] = layoutFull;
        viewRegistry['login'] = layoutFull;
        viewRegistry['ui'] = layoutFull;
        helpRegistry.addUserDoc('index', 'app/core/doc/overview.md');
        helpRegistry.addUserDoc('preferences', 'app/core/doc/preferences.md');
        helpRegistry.addSubTopic('index', 'faq', 'app/core/doc/FAQ.md');
        helpRegistry.addSubTopic('index', 'changes', 'app/core/doc/CHANGES.md');
        helpRegistry.addSubTopic('index', 'developer', 'app/core/doc/developer.md');
        helpRegistry.addDevDoc('Core', 'app/core/doc/coreDeveloper.md');
        helpRegistry.addDevDoc('UI', '#/ui/developerPage');
        helpRegistry.addDevDoc('datatable', 'app/datatable/doc/developer.md');
        helpRegistry.addDevDoc('Force Graph', 'app/forcegraph/doc/developer.md');
        preferencesRegistry.addTab("Core", "app/core/html/corePreferences.html");
        preferencesRegistry.addTab("Plugins", "app/core/html/pluginPreferences.html");
        preferencesRegistry.addTab("Console Logging", "app/core/html/loggingPreferences.html");
        preferencesRegistry.addTab("Editor", "app/ui/html/editorPreferences.html");
        preferencesRegistry.addTab("Jolokia", "app/core/html/jolokiaPreferences.html");
        preferencesRegistry.addTab("Reset", "app/core/html/resetPreferences.html");
        toastr.options = {
            'closeButton': true,
            'showMethod': 'slideDown',
            'hideMethod': 'slideUp'
        };
        var throttledError = {
            level: null,
            message: null,
            action: Core.throttled(function () {
                if (throttledError.level === "WARN") {
                    Core.notification('warning', throttledError.message);
                }
                if (throttledError.level === "ERROR") {
                    Core.notification('error', throttledError.message);
                }
            }, 500)
        };
        window['logInterceptors'].push(function (level, message) {
            throttledError.level = level;
            throttledError.message = message;
            throttledError.action();
        });
        setTimeout(function () {
            Core.checkInjectorLoaded();
            $("#main-body").fadeIn(2000).after(function () {
                Logger.get("Core").info(branding.appName + " started");
                Core.$apply($rootScope);
                $(window).trigger('resize');
            });
        }, 500);
    }]);
})(Core || (Core = {}));
hawtioPluginLoader.addUrl("plugin");
hawtioPluginLoader.addModule(Core.pluginName);
hawtioPluginLoader.addModule('angularFileUpload');
hawtioPluginLoader.registerPreBootstrapTask(function (nextTask) {
    $.support.cors = true;
    nextTask();
});
hawtioPluginLoader.registerPreBootstrapTask(function (nextTask) {
    $("a[title]").tooltip({
        selector: '',
        delay: { show: 1000, hide: 100 }
    });
    nextTask();
});
hawtioPluginLoader.registerPreBootstrapTask(function (nextTask) {
    Core.adjustHeight();
    $(window).resize(Core.adjustHeight);
    nextTask();
});
hawtioPluginLoader.registerPreBootstrapTask(function (nextTask) {
    if (Core._module && Core.isChromeApp()) {
        Core._module.config([
            '$compileProvider',
            function ($compileProvider) {
                $compileProvider.urlSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension):/);
            }
        ]);
    }
    nextTask();
});
var CodeEditor;
(function (CodeEditor) {
    CodeEditor.GlobalCodeMirrorOptions = {
        theme: "default",
        tabSize: 4,
        lineNumbers: true,
        indentWithTabs: true,
        lineWrapping: true,
        autoCloseTags: true
    };
    function detectTextFormat(value) {
        var answer = "text";
        if (value) {
            answer = "javascript";
            var trimmed = value.toString().trimLeft().trimRight();
            if (trimmed && trimmed.first() === '<' && trimmed.last() === '>') {
                answer = "xml";
            }
        }
        return answer;
    }
    CodeEditor.detectTextFormat = detectTextFormat;
    function autoFormatEditor(editor) {
        if (editor) {
            var totalLines = editor.lineCount();
            var start = { line: 0, ch: 0 };
            var end = { line: totalLines - 1, ch: editor.getLine(totalLines - 1).length };
            editor.autoFormatRange(start, end);
            editor.setSelection(start, start);
        }
    }
    CodeEditor.autoFormatEditor = autoFormatEditor;
    function createEditorSettings(options) {
        if (options === void 0) { options = {}; }
        options.extraKeys = options.extraKeys || {};
        (function (mode) {
            mode = mode || { name: "text" };
            if (typeof mode !== "object") {
                mode = { name: mode };
            }
            var modeName = mode.name;
            if (modeName === "javascript") {
                angular.extend(mode, {
                    "json": true
                });
            }
        })(options.mode);
        (function (options) {
            var javascriptFolding = CodeMirror.newFoldFunction(CodeMirror.braceRangeFinder);
            var xmlFolding = CodeMirror.newFoldFunction(CodeMirror.tagRangeFinder);
            var foldFunction = function (codeMirror, line) {
                var mode = codeMirror.getOption("mode");
                var modeName = mode["name"];
                if (!mode || !modeName)
                    return;
                if (modeName === 'javascript') {
                    javascriptFolding(codeMirror, line);
                }
                else if (modeName === "xml" || modeName.startsWith("html")) {
                    xmlFolding(codeMirror, line);
                }
                ;
            };
            options.onGutterClick = foldFunction;
            options.extraKeys = angular.extend(options.extraKeys, {
                "Ctrl-Q": function (codeMirror) {
                    foldFunction(codeMirror, codeMirror.getCursor().line);
                }
            });
        })(options);
        var readOnly = options.readOnly;
        if (!readOnly) {
            options.matchBrackets = true;
        }
        angular.extend(options, CodeEditor.GlobalCodeMirrorOptions);
        return options;
    }
    CodeEditor.createEditorSettings = createEditorSettings;
})(CodeEditor || (CodeEditor = {}));
var UI;
(function (UI) {
    UI.log = Logger.get("UI");
    UI.scrollBarWidth = null;
    function findParentWith($scope, attribute) {
        if (attribute in $scope) {
            return $scope;
        }
        if (!$scope.$parent) {
            return null;
        }
        return findParentWith($scope.$parent, attribute);
    }
    UI.findParentWith = findParentWith;
    function getIfSet(attribute, $attr, def) {
        if (attribute in $attr) {
            var wantedAnswer = $attr[attribute];
            if (wantedAnswer && !wantedAnswer.isBlank()) {
                return wantedAnswer;
            }
        }
        return def;
    }
    UI.getIfSet = getIfSet;
    function observe($scope, $attrs, key, defValue, callbackFunc) {
        if (callbackFunc === void 0) { callbackFunc = null; }
        $attrs.$observe(key, function (value) {
            if (!angular.isDefined(value)) {
                $scope[key] = defValue;
            }
            else {
                $scope[key] = value;
            }
            if (angular.isDefined(callbackFunc) && callbackFunc) {
                callbackFunc($scope[key]);
            }
        });
    }
    UI.observe = observe;
    function getScrollbarWidth() {
        if (!angular.isDefined(UI.scrollBarWidth)) {
            var div = document.createElement('div');
            div.innerHTML = '<div style="width:50px;height:50px;position:absolute;left:-50px;top:-50px;overflow:auto;"><div style="width:1px;height:100px;"></div></div>';
            div = div.firstChild;
            document.body.appendChild(div);
            UI.scrollBarWidth = div.offsetWidth - div.clientWidth;
            document.body.removeChild(div);
        }
        return UI.scrollBarWidth;
    }
    UI.getScrollbarWidth = getScrollbarWidth;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI.pluginName = 'hawtio-ui';
    UI.templatePath = 'app/ui/html/';
    UI._module = angular.module(UI.pluginName, ['bootstrap', 'ngResource', 'ui', 'ui.bootstrap']);
    UI._module.config(["$routeProvider", function ($routeProvider) {
        $routeProvider.when('/ui/developerPage', { templateUrl: UI.templatePath + 'developerPage.html', reloadOnSearch: false });
    }]);
    UI._module.factory('UI', function () {
        return UI;
    });
    UI._module.factory('marked', function () {
        marked.setOptions({
            gfm: true,
            tables: true,
            breaks: false,
            pedantic: true,
            sanitize: false,
            smartLists: true,
            langPrefix: 'language-'
        });
        return marked;
    });
    UI._module.directive('compile', ['$compile', function ($compile) {
        return function (scope, element, attrs) {
            scope.$watch(function (scope) {
                return scope.$eval(attrs.compile);
            }, function (value) {
                element.html(value);
                $compile(element.contents())(scope);
            });
        };
    }]);
    UI._module.controller("CodeEditor.PreferencesController", ["$scope", "localStorage", "$templateCache", function ($scope, localStorage, $templateCache) {
        $scope.exampleText = $templateCache.get("exampleText");
        $scope.codeMirrorEx = $templateCache.get("codeMirrorExTemplate");
        $scope.javascript = "javascript";
        $scope.preferences = CodeEditor.GlobalCodeMirrorOptions;
        $scope.$watch("preferences", function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $scope.codeMirrorEx += " ";
                localStorage['CodeMirrorOptions'] = angular.toJson(angular.extend(CodeEditor.GlobalCodeMirrorOptions, $scope.preferences));
            }
        }, true);
    }]);
    UI._module.run(["localStorage", function (localStorage) {
        var opts = localStorage['CodeMirrorOptions'];
        if (opts) {
            opts = angular.fromJson(opts);
            CodeEditor.GlobalCodeMirrorOptions = angular.extend(CodeEditor.GlobalCodeMirrorOptions, opts);
        }
    }]);
    hawtioPluginLoader.addModule(UI.pluginName);
})(UI || (UI = {}));
var UI;
(function (UI) {
    function hawtioDropDown($templateCache) {
        return {
            restrict: 'A',
            replace: true,
            templateUrl: UI.templatePath + 'dropDown.html',
            scope: {
                config: '=hawtioDropDown'
            },
            controller: ["$scope", "$element", "$attrs", function ($scope, $element, $attrs) {
                if (!$scope.config) {
                    $scope.config = {};
                }
                if (!('open' in $scope.config)) {
                    $scope.config['open'] = false;
                }
                $scope.action = function (config, $event) {
                    if ('items' in config && !('action' in config)) {
                        config.open = !config.open;
                        $event.preventDefault();
                        $event.stopPropagation();
                    }
                    else if ('action' in config) {
                        var action = config['action'];
                        if (angular.isFunction(action)) {
                            action();
                        }
                        else if (angular.isString(action)) {
                            $scope.$parent.$eval(action, {
                                config: config,
                                '$event': $event
                            });
                        }
                    }
                };
                $scope.$watch('config.items', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        $scope.menuStyle = $scope.menuStyle + " ";
                    }
                }, true);
                $scope.submenu = function (config) {
                    if (config && config.submenu) {
                        return "sub-menu";
                    }
                    return "";
                };
                $scope.icon = function (config) {
                    if (config && !Core.isBlank(config.icon)) {
                        return config.icon;
                    }
                    else {
                        return 'icon-spacer';
                    }
                };
                $scope.open = function (config) {
                    if (config && !config.open) {
                        return '';
                    }
                    return 'open';
                };
            }],
            link: function ($scope, $element, $attrs) {
                $scope.menuStyle = $templateCache.get("withsubmenus.html");
                if ('processSubmenus' in $attrs) {
                    if (!Core.parseBooleanValue($attrs['processSubmenus'])) {
                        $scope.menuStyle = $templateCache.get("withoutsubmenus.html");
                    }
                }
            }
        };
    }
    UI.hawtioDropDown = hawtioDropDown;
    UI._module.directive('hawtioDropDown', ["$templateCache", UI.hawtioDropDown]);
})(UI || (UI = {}));
var ContainerHelpers;
(function (ContainerHelpers) {
    ContainerHelpers.NO_LOCATION = "No Location";
    function extractLocations(containers) {
        var locations = containers.map(function (container) {
            if (Core.isBlank(container['location'])) {
                return ContainerHelpers.NO_LOCATION;
            }
            else {
                return container['location'];
            }
        });
        locations.push(ContainerHelpers.NO_LOCATION);
        locations = locations.unique().sortBy('');
        locations = locations.exclude(function (location) {
            return Core.isBlank(location);
        });
        return locations;
    }
    ContainerHelpers.extractLocations = extractLocations;
    function getCreateLocationDialog($scope, $dialog) {
        return Fabric.getCreateLocationDialog($dialog, {
            selectedContainers: function () {
                return $scope.selectedContainers;
            },
            callbacks: function () {
                return {
                    success: function (response) {
                        Core.$apply($scope);
                    },
                    error: function (response) {
                        Core.$apply($scope);
                    }
                };
            }
        });
    }
    ContainerHelpers.getCreateLocationDialog = getCreateLocationDialog;
    function buildLocationMenu($scope, jolokia, locations) {
        var locationMenu = {
            icon: 'icon-location-arrow',
            title: 'Set Location',
            items: []
        };
        var menuItems = [];
        locations.forEach(function (location) {
            menuItems.push({
                title: location,
                action: function () {
                    $scope.selectedContainers.forEach(function (container) {
                        var arg = location;
                        if (arg === ContainerHelpers.NO_LOCATION) {
                            arg = "";
                        }
                        Fabric.setContainerProperty(jolokia, container.id, 'location', arg, function () {
                            Core.$apply($scope);
                        }, function () {
                            Core.$apply($scope);
                        });
                    });
                }
            });
        });
        menuItems.push({
            title: "New...",
            action: function () {
                $scope.createLocationDialog.open();
            }
        });
        locationMenu.items = menuItems;
        return locationMenu;
    }
    ContainerHelpers.buildLocationMenu = buildLocationMenu;
    function isCurrentContainer(container) {
        if (!container) {
            return false;
        }
        if (Core.isBlank(Fabric.currentContainerId)) {
            return false;
        }
        if (angular.isObject(container)) {
            return container['id'] === Fabric.currentContainerId;
        }
        if (angular.isString(container)) {
            return container === Fabric.currentContainerId;
        }
        return false;
    }
    ContainerHelpers.isCurrentContainer = isCurrentContainer;
    ;
    function canConnect(container) {
        if (!container) {
            return false;
        }
        if (Core.isBlank(container['jolokiaUrl'])) {
            return false;
        }
        if (!Core.parseBooleanValue(container['alive'])) {
            return false;
        }
        return true;
    }
    ContainerHelpers.canConnect = canConnect;
    ;
    function statusTitle(container) {
        var answer = 'Alive';
        if (!container.alive) {
            answer = 'Not Running';
        }
        else {
            answer += ' - ' + Core.humanizeValue(container.provisionResult);
        }
        return answer;
    }
    ContainerHelpers.statusTitle = statusTitle;
    function statusIcon(row) {
        if (row) {
            switch (row.provisionResult) {
                case 'success':
                    if (row.alive) {
                        return "green icon-play-circle";
                    }
                    else {
                        return "orange icon-off";
                    }
                case 'downloading':
                    return "icon-download-alt";
                case 'installing':
                    return "icon-hdd";
                case 'analyzing':
                case 'finalizing':
                    return "icon-refresh icon-spin";
                case 'resolving':
                    return "icon-sitemap";
                case 'error':
                    return "red icon-warning-sign";
            }
            if (!row.alive) {
                return "orange icon-off";
            }
        }
        return "icon-refresh icon-spin";
    }
    ContainerHelpers.statusIcon = statusIcon;
    function gotoContainer($location, container) {
        $location.path('/fabric/container/' + container.id);
    }
    ContainerHelpers.gotoContainer = gotoContainer;
    function doDeleteContainer($scope, jolokia, name, onDelete) {
        if (onDelete === void 0) { onDelete = null; }
        Fabric.destroyContainer(jolokia, name, function () {
            if (onDelete) {
                onDelete();
            }
            Core.$apply($scope);
        });
    }
    ContainerHelpers.doDeleteContainer = doDeleteContainer;
    function doStartContainer($scope, jolokia, name) {
        if ($scope.fabricVerboseNotifications) {
            Core.notification('info', "Starting " + name);
        }
        Fabric.startContainer(jolokia, name, function () {
            Core.$apply($scope);
        });
    }
    ContainerHelpers.doStartContainer = doStartContainer;
    function doStopContainer($scope, jolokia, name) {
        if ($scope.fabricVerboseNotifications) {
            Core.notification('info', "Stopping " + name);
        }
        Fabric.stopContainer(jolokia, name, function () {
            Core.$apply($scope);
        });
    }
    ContainerHelpers.doStopContainer = doStopContainer;
    function stopContainers($scope, jolokia, c) {
        c.forEach(function (c) { return doStopContainer($scope, jolokia, c.id); });
    }
    ContainerHelpers.stopContainers = stopContainers;
    function startContainers($scope, jolokia, c) {
        c.forEach(function (c) { return doStartContainer($scope, jolokia, c.id); });
    }
    ContainerHelpers.startContainers = startContainers;
    function anyStartable(containers) {
        return containers.length > 0 && containers.any(function (container) {
            var answer = false;
            if (!container.alive) {
                answer = true;
                switch (container.provisionResult) {
                    case 'downloading':
                    case 'installing':
                    case 'analyzing':
                    case 'finalizing':
                    case 'resolving':
                        answer = false;
                }
            }
            return answer;
        });
    }
    ContainerHelpers.anyStartable = anyStartable;
    function anyStoppable(containers) {
        return containers.length > 0 && containers.any(function (c) { return c.alive === true; });
    }
    ContainerHelpers.anyStoppable = anyStoppable;
    function allAlive(containers, state) {
        if (state === void 0) { state = true; }
        return containers.length > 0 && containers.every(function (c) { return c.alive === state; });
    }
    ContainerHelpers.allAlive = allAlive;
    function decorate($scope, $location, jolokia) {
        if ($scope.containerHelpersAdded) {
            return;
        }
        $scope.containerHelpersAdded = true;
        $scope.isCurrentContainer = isCurrentContainer;
        $scope.canConnect = canConnect;
        $scope.getStatusTitle = statusTitle;
        $scope.showContainer = function (container) {
            gotoContainer($location, container);
        };
        $scope.statusIcon = statusIcon;
        $scope.everySelectionAlive = function (state) {
            return allAlive($scope.selectedContainers, state);
        };
        $scope.anySelectionStartable = function () {
            return anyStartable($scope.selectedContainers);
        };
        $scope.anySelectionStoppable = function () {
            return anyStoppable($scope.selectedContainers);
        };
        $scope.startContainer = function (name) {
            doStartContainer($scope, jolokia, name);
        };
        $scope.stopContainer = function (name) {
            doStopContainer($scope, jolokia, name);
        };
        $scope.startSelectedContainers = function () {
            startContainers($scope, jolokia, $scope.selectedContainers);
        };
        $scope.stopSelectedContainers = function () {
            stopContainers($scope, jolokia, $scope.selectedContainers);
        };
    }
    ContainerHelpers.decorate = decorate;
})(ContainerHelpers || (ContainerHelpers = {}));
var UI;
(function (UI) {
    var Dialog = (function () {
        function Dialog() {
            this.show = false;
            this.options = {
                backdropFade: true,
                dialogFade: true
            };
        }
        Dialog.prototype.open = function () {
            this.show = true;
        };
        Dialog.prototype.close = function () {
            this.show = false;
            this.removeBackdropFadeDiv();
            setTimeout(this.removeBackdropFadeDiv, 100);
        };
        Dialog.prototype.removeBackdropFadeDiv = function () {
            $("div.modal-backdrop").remove();
        };
        return Dialog;
    })();
    UI.Dialog = Dialog;
    function multiItemConfirmActionDialog(options) {
        var $dialog = Core.injector.get("$dialog");
        return $dialog.dialog({
            resolve: {
                options: function () {
                    return options;
                }
            },
            templateUrl: 'app/ui/html/multiItemConfirmActionDialog.html',
            controller: ["$scope", "dialog", "options", function ($scope, dialog, options) {
                $scope.options = options;
                $scope.close = function (result) {
                    dialog.close();
                    options.onClose(result);
                };
            }]
        });
    }
    UI.multiItemConfirmActionDialog = multiItemConfirmActionDialog;
})(UI || (UI = {}));
var PluginHelpers;
(function (PluginHelpers) {
    function createControllerFunction(_module, pluginName) {
        return function (name, inlineAnnotatedConstructor) {
            return _module.controller(pluginName + '.' + name, inlineAnnotatedConstructor);
        };
    }
    PluginHelpers.createControllerFunction = createControllerFunction;
    function createRoutingFunction(templateUrl) {
        return function (templateName, reloadOnSearch) {
            if (reloadOnSearch === void 0) { reloadOnSearch = true; }
            return {
                templateUrl: UrlHelpers.join(templateUrl, templateName),
                reloadOnSearch: reloadOnSearch
            };
        };
    }
    PluginHelpers.createRoutingFunction = createRoutingFunction;
})(PluginHelpers || (PluginHelpers = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.LoginController", ["$scope", "jolokia", "userDetails", "jolokiaUrl", "workspace", "localStorage", "branding", "postLoginTasks", function ($scope, jolokia, userDetails, jolokiaUrl, workspace, localStorage, branding, postLoginTasks) {
        jolokia.stop();
        $scope.userDetails = userDetails;
        $scope.entity = {
            username: '',
            password: ''
        };
        $scope.backstretch = $.backstretch(branding.loginBg);
        $scope.rememberMe = false;
        if ('userDetails' in localStorage) {
            $scope.rememberMe = true;
            var details = angular.fromJson(localStorage['userDetails']);
            $scope.entity.username = details.username;
            $scope.entity.password = details.password;
        }
        $scope.branding = branding;
        $scope.$watch('userDetails', function (newValue) {
            if (newValue.username) {
                $scope.entity.username = newValue.username;
            }
            if (newValue.password) {
                $scope.entity.password = newValue.password;
            }
        }, true);
        $scope.$on('$routeChangeStart', function () {
            if ($scope.backstretch) {
                $scope.backstretch.destroy();
            }
        });
        $scope.doLogin = function () {
            if (jolokiaUrl) {
                var url = "auth/login/";
                if ($scope.entity.username.trim() != '') {
                    $.ajax(url, {
                        type: "POST",
                        success: function (response) {
                            userDetails.username = $scope.entity.username;
                            userDetails.password = $scope.entity.password;
                            userDetails.loginDetails = response;
                            if ($scope.rememberMe) {
                                localStorage['userDetails'] = angular.toJson(userDetails);
                            }
                            else {
                                delete localStorage['userDetails'];
                            }
                            jolokia.start();
                            workspace.loadTree();
                            Core.executePostLoginTasks();
                            Core.$apply($scope);
                        },
                        error: function (xhr, textStatus, error) {
                            switch (xhr.status) {
                                case 401:
                                    Core.notification('error', 'Failed to log in, ' + error);
                                    break;
                                case 403:
                                    Core.notification('error', 'Failed to log in, ' + error);
                                    break;
                                default:
                                    Core.notification('error', 'Failed to log in, ' + error);
                                    break;
                            }
                            Core.$apply($scope);
                        },
                        beforeSend: function (xhr) {
                            xhr.setRequestHeader('Authorization', Core.getBasicAuthHeader($scope.entity.username, $scope.entity.password));
                        }
                    });
                }
            }
        };
    }]);
})(Core || (Core = {}));
var API;
(function (API) {
    API.log = Logger.get("API");
    API.wadlNamespace = "http://schemas.xmlsoap.org/wsdl/";
    function loadXml(url, onXml) {
        if (url) {
            API.log.info("Loading XML: " + url);
            var ajaxParams = {
                type: "GET",
                url: url,
                beforeSend: function (xhr) {
                    xhr.setRequestHeader('Authorization', null);
                },
                dataType: "xml",
                contextType: "text/xml",
                success: onXml,
                error: function (jqXHR, textStatus, errorThrow) {
                    API.log.error("Failed to query XML for: " + url + " status:" + textStatus + " error: " + errorThrow);
                }
            };
            $.ajax(ajaxParams);
        }
    }
    API.loadXml = loadXml;
    var wadlXmlToJavaConfig = {};
    function parseJson(json) {
        var answer = null;
        try {
            answer = JSON.parse(json);
        }
        catch (e) {
            API.log.info("Failed to parse JSON " + e);
            API.log.info("JSON: " + json);
        }
        return answer;
    }
    API.parseJson = parseJson;
    function initScope($scope, $location, jolokia) {
        var search = $location.search();
        $scope.container = search["container"];
        $scope.objectName = search["objectName"];
        $scope.showHide = function (resource) {
            if (resource) {
                resource.hide = resource.hide ? false : true;
            }
        };
        $scope.showOperations = function (resource) {
            showHideOperations(resource, false);
        };
        $scope.expandOperations = function (resource) {
            showHideOperations(resource, true);
        };
        function showHideOperations(resource, flag) {
            if (resource) {
                resource.hide = false;
                angular.forEach(resource.resource, function (childResource) {
                    showHideOperations(childResource, flag);
                });
                angular.forEach(resource.method || resource.operations, function (method) {
                    method.expanded = flag;
                });
            }
        }
        $scope.autoFormat = function (codeMirror) {
            if (!codeMirror) {
                codeMirror = findChildScopeValue($scope, "codeMirror");
            }
            if (codeMirror) {
                setTimeout(function () {
                    CodeEditor.autoFormatEditor(codeMirror);
                }, 50);
            }
        };
        function findChildScopeValue(scope, name) {
            var answer = scope[name];
            var childScope = scope.$$childHead;
            while (childScope && !answer) {
                answer = findChildScopeValue(childScope, name);
                childScope = childScope.$$nextSibling;
            }
            return answer;
        }
        if ($scope.container && $scope.objectName) {
            Fabric.containerJolokia(jolokia, $scope.container, function (remoteJolokia) {
                $scope.remoteJolokia = remoteJolokia;
                if (remoteJolokia) {
                    API.loadJsonSchema(remoteJolokia, $scope.objectName, function (jsonSchema) {
                        $scope.jsonSchema = jsonSchema;
                        Core.$apply($scope);
                    });
                }
                else {
                    API.log.info("No Remote Jolokia!");
                }
            });
        }
        else {
            API.log.info("No container or objectName");
        }
        API.log.info("container: " + $scope.container + " objectName: " + $scope.objectName + " url: " + $scope.url);
    }
    API.initScope = initScope;
    function loadJsonSchema(jolokia, mbean, onJsonSchemaFn) {
        function onResults(response) {
            var schema = {};
            if (response) {
                var json = response;
                if (json) {
                    schema = parseJson(json);
                }
            }
            onJsonSchemaFn(schema);
        }
        if (mbean) {
            return jolokia.execute(mbean, "getJSONSchema", onSuccess(onResults));
        }
        else {
            var schema = {};
            onJsonSchemaFn(schema);
            return schema;
        }
    }
    API.loadJsonSchema = loadJsonSchema;
    function onWadlXmlLoaded(response) {
        var root = response.documentElement;
        var output = {};
        return API.convertWadlToJson(root, output);
    }
    API.onWadlXmlLoaded = onWadlXmlLoaded;
    function convertWadlToJson(element, obj) {
        if (obj === void 0) { obj = {}; }
        return API.convertXmlToJson(element, obj, wadlXmlToJavaConfig);
    }
    API.convertWadlToJson = convertWadlToJson;
    function convertWadlJsonToSwagger(object) {
        var apis = [];
        var basePath = null;
        var resourcePath = null;
        var resources = Core.pathGet(object, ["resources", 0]);
        if (resources) {
            basePath = resources.base;
            angular.forEach(resources.resource, function (resource) {
                var path = resource.path;
                var operations = [];
                angular.forEach(resource.method, function (method) {
                    var name = method.name;
                    var responseMessages = [];
                    var parameters = [];
                    operations.push({
                        "method": method.name,
                        "summary": method.summary,
                        "notes": method.notes,
                        "nickname": method.nickname,
                        "type": method.type,
                        "parameters": parameters,
                        "produces": [
                            "application/json"
                        ],
                        "responseMessages": responseMessages
                    });
                });
                apis.push({
                    path: path,
                    operations: operations
                });
            });
        }
        return {
            "apiVersion": "1.0",
            "swaggerVersion": "1.2",
            "basePath": basePath,
            "resourcePath": resourcePath,
            "produces": [
                "application/json"
            ],
            apis: apis
        };
    }
    API.convertWadlJsonToSwagger = convertWadlJsonToSwagger;
    function nodeName(owner, node) {
        return node ? node.localName : null;
    }
    function convertXmlToJson(element, obj, config) {
        var elementProperyFn = config.elementToPropertyName || nodeName;
        var attributeProperyFn = config.attributeToPropertyName || nodeName;
        angular.forEach(element.childNodes, function (child) {
            if (child.nodeType === 1) {
                var propertyName = elementProperyFn(element, child);
                if (propertyName) {
                    var array = obj[propertyName] || [];
                    if (!angular.isArray(array)) {
                        array = [array];
                    }
                    var value = {};
                    convertXmlToJson(child, value, config);
                    array.push(value);
                    obj[propertyName] = array;
                }
            }
        });
        angular.forEach(element.attributes, function (attr) {
            var propertyName = attributeProperyFn(element, attr);
            if (propertyName) {
                var value = attr.nodeValue;
                obj[propertyName] = value;
            }
        });
        return obj;
    }
    API.convertXmlToJson = convertXmlToJson;
    function concatArrays(arrays) {
        var answer = [];
        angular.forEach(arrays, function (array) {
            if (array) {
                if (angular.isArray(array)) {
                    answer = answer.concat(array);
                }
                else {
                    answer.push(array);
                }
            }
        });
        return answer;
    }
    API.concatArrays = concatArrays;
    function addObjectNameProperties(object) {
        var objectName = object["objectName"];
        if (objectName) {
            var properties = Core.objectNameProperties(objectName);
            if (properties) {
                angular.forEach(properties, function (value, key) {
                    if (!object[key]) {
                        object[key] = value;
                    }
                });
            }
        }
        return null;
    }
    function processApiData($scope, json, podURL, path) {
        if (path === void 0) { path = ""; }
        var array = [];
        angular.forEach(json, function (value, key) {
            var childPath = path + "/" + key;
            function addParameters(href) {
                angular.forEach(["podId", "port", "objectName"], function (name) {
                    var param = value[name];
                    if (param) {
                        href += "&" + name + "=" + encodeURIComponent(param);
                    }
                });
                return href;
            }
            var path = value["path"];
            var url = value["url"];
            if (url) {
                addObjectNameProperties(value);
                value["serviceName"] = Core.trimQuotes(value["service"]) || value["containerName"];
                var podId = value["podId"];
                var prefix = "";
                if (podId) {
                    var port = value["port"] || 8080;
                    prefix = podURL + podId + "/" + port;
                }
                function addPrefix(text) {
                    return (text) ? prefix + text : null;
                }
                function maybeUseProxy(value) {
                    if (value) {
                        return Core.useProxyIfExternal(value);
                    }
                    else {
                        return value;
                    }
                }
                var apidocs = maybeUseProxy(value["swaggerUrl"]) || addPrefix(value["swaggerPath"]);
                var wadl = maybeUseProxy(value["wadlUrl"]) || addPrefix(value["wadlPath"]);
                var wsdl = maybeUseProxy(value["wsdlUrl"]) || addPrefix(value["wsdlPath"]);
                if (apidocs) {
                    value["apidocsHref"] = addParameters("/hawtio-swagger/index.html?baseUri=" + apidocs);
                }
                if (wadl) {
                    value["wadlHref"] = addParameters("#/api/wadl?wadl=" + encodeURIComponent(wadl));
                }
                if (wsdl) {
                    value["wsdlHref"] = addParameters("#/api/wsdl?wsdl=" + encodeURIComponent(wsdl));
                }
            }
            array.push(value);
        });
        $scope.apis = array;
        $scope.initDone = true;
    }
    API.processApiData = processApiData;
})(API || (API = {}));
var API;
(function (API) {
    API.pluginName = 'api';
    API.templatePath = 'app/' + API.pluginName + '/html/';
    API._module = angular.module(API.pluginName, ['bootstrap', 'hawtioCore', 'hawtio-ui']);
    API._module.config(["$routeProvider", function ($routeProvider) {
        $routeProvider.when('/api/pods', { templateUrl: 'app/api/html/apiPods.html' }).when('/api/services', { templateUrl: 'app/api/html/apiServices.html' }).when('/api/wsdl', { templateUrl: 'app/api/html/wsdl.html' }).when('/api/wadl', { templateUrl: 'app/api/html/wadl.html' });
    }]);
    API._module.run(["$location", "workspace", "viewRegistry", "layoutFull", "helpRegistry", "ServiceRegistry", function ($location, workspace, viewRegistry, layoutFull, helpRegistry, ServiceRegistry) {
        viewRegistry['api/pods'] = API.templatePath + "layoutApis.html";
        viewRegistry['api/services'] = API.templatePath + "layoutApis.html";
        viewRegistry['api'] = layoutFull;
        workspace.topLevelTabs.push({
            id: 'apis.index',
            content: 'APIs',
            title: 'View the available APIs inside this fabric',
            isValid: function (workspace) { return Service.hasService(ServiceRegistry, "api-registry") && Kubernetes.isKubernetes(workspace); },
            href: function () { return '#/api/services'; },
            isActive: function (workspace) { return workspace.isLinkActive('api/'); }
        });
    }]);
    hawtioPluginLoader.addModule(API.pluginName);
})(API || (API = {}));
var API;
(function (API) {
    API._module.controller("API.ApiPodsController", ["$scope", "localStorage", "$routeParams", "$location", "jolokia", "workspace", "$compile", "$templateCache", "$http", function ($scope, localStorage, $routeParams, $location, jolokia, workspace, $compile, $templateCache, $http) {
        $scope.path = "apis";
        $scope.apis = null;
        $scope.selectedApis = [];
        $scope.initDone = false;
        var endpointsPodsURL = Core.url("/service/api-registry/endpoints/pods");
        var podURL = Core.url("/pod/");
        $scope.apiOptions = {
            data: 'apis',
            showFilter: false,
            showColumnMenu: false,
            filterOptions: {
                filterText: "",
                useExternalFilter: false
            },
            selectedItems: $scope.selectedApis,
            rowHeight: 32,
            showSelectionCheckbox: false,
            selectWithCheckboxOnly: true,
            columnDefs: [
                {
                    field: 'serviceName',
                    displayName: 'Endpoint',
                    width: "***"
                },
                {
                    field: 'contracts',
                    displayName: 'APIs',
                    cellTemplate: $templateCache.get("apiContractLinksTemplate.html"),
                    width: "*"
                },
                {
                    field: 'url',
                    displayName: 'URL',
                    cellTemplate: $templateCache.get("apiUrlTemplate.html"),
                    width: "***"
                },
                {
                    field: 'podId',
                    displayName: 'Pod',
                    cellTemplate: $templateCache.get("apiPodLinkTemplate.html"),
                    width: "*"
                }
            ]
        };
        function matchesFilter(text) {
            var filter = $scope.searchFilter;
            return !filter || (text && text.has(filter));
        }
        function loadData() {
            var restURL = endpointsPodsURL;
            $http.get(restURL).success(function (data) {
                createFlatList(restURL, data);
            }).error(function (data) {
                API.log.debug("Error fetching image repositories:", data);
                createFlatList(restURL, null);
            });
        }
        loadData();
        function createFlatList(restURL, json, path) {
            if (path === void 0) { path = ""; }
            return API.processApiData($scope, json, podURL, path);
        }
    }]);
})(API || (API = {}));
var API;
(function (API) {
    API._module.controller("API.ApiServicesController", ["$scope", "localStorage", "$routeParams", "$location", "jolokia", "workspace", "$compile", "$templateCache", "$http", function ($scope, localStorage, $routeParams, $location, jolokia, workspace, $compile, $templateCache, $http) {
        $scope.path = "apis";
        $scope.apis = null;
        $scope.selectedApis = [];
        $scope.initDone = false;
        var endpointsPodsURL = Core.url("/service/api-registry/endpoints/services");
        var podURL = Core.url("/pod/");
        $scope.apiOptions = {
            data: 'apis',
            showFilter: false,
            showColumnMenu: false,
            filterOptions: {
                filterText: "",
                useExternalFilter: false
            },
            selectedItems: $scope.selectedApis,
            rowHeight: 32,
            showSelectionCheckbox: false,
            selectWithCheckboxOnly: true,
            columnDefs: [
                {
                    field: 'serviceName',
                    displayName: 'Service',
                    cellTemplate: $templateCache.get("apiServiceLinkTemplate.html"),
                    width: "***"
                },
                {
                    field: 'contracts',
                    displayName: 'APIs',
                    cellTemplate: $templateCache.get("apiContractLinksTemplate.html"),
                    width: "*"
                },
                {
                    field: 'url',
                    displayName: 'URL',
                    cellTemplate: $templateCache.get("apiUrlTemplate.html"),
                    width: "***"
                }
            ]
        };
        function matchesFilter(text) {
            var filter = $scope.searchFilter;
            return !filter || (text && text.has(filter));
        }
        function loadData() {
            var restURL = endpointsPodsURL;
            $http.get(restURL).success(function (data) {
                createFlatList(restURL, data);
            }).error(function (data) {
                API.log.debug("Error fetching image repositories:", data);
                createFlatList(restURL, null);
            });
        }
        loadData();
        function createFlatList(restURL, json, path) {
            if (path === void 0) { path = ""; }
            return API.processApiData($scope, json, podURL, path);
        }
    }]);
})(API || (API = {}));
var API;
(function (API) {
    API._module.controller("API.WadlViewController", ["$scope", "$location", "$http", "jolokia", function ($scope, $location, $http, jolokia) {
        API.initScope($scope, $location, jolokia);
        var search = $location.search();
        $scope.url = search["wadl"];
        $scope.podId = search["podId"];
        $scope.port = search["port"];
        $scope.$watch("apidocs", enrichApiDocsWithSchema);
        $scope.$watch("jsonSchema", enrichApiDocsWithSchema);
        API.loadXml($scope.url, onWsdl);
        $scope.tryInvoke = function (resource, method) {
            var useProxy = true;
            if (resource) {
                var path = resource.fullPath || resource.path;
                if (path) {
                    if ($scope.podId) {
                        var idx = path.indexOf("://");
                        if (idx > 0) {
                            var pathWithoutProtocol = path.substring(idx + 3);
                            var idx = pathWithoutProtocol.indexOf("/");
                            if (idx > 0) {
                                path = "/hawtio/pod/" + $scope.podId + ($scope.port ? "/" + $scope.port : "") + pathWithoutProtocol.substring(idx);
                                useProxy = false;
                            }
                        }
                    }
                    angular.forEach(resource.param, function (param) {
                        var name = param.name;
                        if (name) {
                            var value = param.value;
                            if (angular.isUndefined(value) || value === null) {
                                value = "";
                            }
                            value = value.toString();
                            API.log.debug("replacing " + name + " with '" + value + "'");
                            path = path.replace(new RegExp("{" + name + "}", "g"), value);
                        }
                    });
                    var url = useProxy ? Core.useProxyIfExternal(path) : path;
                    API.log.info("Lets invoke resource: " + url);
                    var methodName = method.name || "GET";
                    method.invoke = {
                        url: url,
                        running: true
                    };
                    var requestData = {
                        method: methodName,
                        url: url,
                        headers: {}
                    };
                    if (methodName === "POST" || methodName === "PUT") {
                        angular.forEach(method.request, function (request) {
                            if (!requestData["data"]) {
                                requestData["data"] = request.value;
                            }
                            if (!requestData.headers["Content-Type"]) {
                                requestData.headers["Content-Type"] = request.contentType;
                            }
                        });
                    }
                    API.log.info("About to make request: " + angular.toJson(requestData));
                    $http(requestData).success(function (data, status, headers, config) {
                        API.log.info("Worked!" + data);
                        method.invoke = {
                            url: url,
                            realUrl: path,
                            success: true,
                            data: data,
                            dataMode: textFormat(headers),
                            status: status,
                            headers: headers(),
                            config: config
                        };
                        Core.$apply($scope);
                    }).error(function (data, status, headers, config) {
                        API.log.info("Failed: " + status);
                        method.invoke = {
                            url: url,
                            realUrl: path,
                            data: data,
                            dataMode: textFormat(headers),
                            status: status,
                            headers: headers(),
                            config: config
                        };
                        Core.$apply($scope);
                    });
                }
            }
        };
        function textFormat(headers) {
            return contentTypeTextFormat(headers("content-type"));
        }
        function contentTypeTextFormat(contentType) {
            if (contentType) {
                if (contentType.endsWith("xml")) {
                    return "xml";
                }
                if (contentType.endsWith("html")) {
                    return "html";
                }
                if (contentType.endsWith("json")) {
                    return "json";
                }
            }
            return null;
        }
        function enrichApiDocsWithSchema() {
            var apidocs = $scope.apidocs;
            var jsonSchema = $scope.jsonSchema;
            if (apidocs) {
                enrichResources(jsonSchema, apidocs.resources, $scope.parentUri);
            }
        }
        function enrichResources(jsonSchema, resources, parentUri) {
            if (parentUri === void 0) { parentUri = null; }
            angular.forEach(resources, function (resource) {
                var base = resource.base;
                if (base) {
                    if (parentUri) {
                        if (base) {
                            var idx = base.indexOf("/");
                            if (idx > 0) {
                                base = parentUri + base.substring(idx);
                            }
                        }
                    }
                }
                else {
                    base = parentUri;
                }
                var path = resource.path;
                if (base && path) {
                    if (!base.endsWith("/") && !path.startsWith("/")) {
                        base += "/";
                    }
                    base += path;
                    resource["fullPath"] = base;
                }
                var childResources = resource.resource;
                if (childResources) {
                    enrichResources(jsonSchema, childResources, base);
                }
                angular.forEach(API.concatArrays([resource.method, resource.operation]), function (method) {
                    var request = method.request;
                    if (request) {
                        var count = request.count(function (n) { return n["representation"]; });
                        if (!count) {
                            delete method.request;
                        }
                    }
                    angular.forEach(API.concatArrays([method.request, method.response]), function (object) {
                        var element = object["element"];
                        var representations = object["representation"];
                        if (representations) {
                            var mediaTypes = representations.map(function (r) { return r["mediaType"]; });
                            object["mediaTypes"] = mediaTypes;
                            if (mediaTypes && mediaTypes.length) {
                                object["contentType"] = mediaTypes[0];
                            }
                        }
                        angular.forEach(representations, function (representation) {
                            if (!element) {
                                element = representation["element"];
                            }
                            enrichRepresentation(jsonSchema, representation);
                        });
                        if (element) {
                            object["element"] = element;
                        }
                    });
                });
            });
        }
        function enrichRepresentation(jsonSchema, representation) {
            var defs = jsonSchema ? jsonSchema["definitions"] : null;
            if (defs && representation) {
                var contentType = representation["mediaType"];
                if (contentType) {
                    representation["dataMode"] = contentTypeTextFormat(contentType);
                }
                var element = representation["element"];
                if (element) {
                    var idx = element.indexOf(':');
                    if (idx >= 0) {
                        element = element.substring(idx + 1);
                    }
                    var elementPostfix = "." + element;
                    var foundDef = null;
                    angular.forEach(defs, function (value, key) {
                        if (!foundDef && (key === element || key.endsWith(elementPostfix))) {
                            foundDef = value;
                            representation["schema"] = foundDef;
                            representation["typeName"] = element;
                            representation["javaClass"] = key;
                        }
                    });
                }
            }
        }
        function onWsdl(response) {
            $scope.apidocs = API.onWadlXmlLoaded(response);
            Core.$apply($scope);
        }
    }]);
})(API || (API = {}));
var API;
(function (API) {
    API._module.controller("API.WsdlViewController", ["$scope", "$location", "jolokia", function ($scope, $location, jolokia) {
        var log = Logger.get("API");
        API.initScope($scope, $location, jolokia);
        var wsdlNamespace = "http://schemas.xmlsoap.org/wsdl/";
        $scope.url = $location.search()["wsdl"];
        API.loadXml($scope.url, onWsdl);
        $scope.$watch("services", enrichApiDocsWithSchema);
        $scope.$watch("jsonSchema", enrichApiDocsWithSchema);
        function enrichApiDocsWithSchema() {
            var services = $scope.services;
            var jsonSchema = $scope.jsonSchema;
            if (services && jsonSchema) {
                log.info("We have services and jsonSchema!");
                enrichServices(jsonSchema, services);
            }
        }
        function enrichServices(jsonSchema, services) {
            angular.forEach(services, function (service) {
                angular.forEach(service.operations, function (method) {
                    angular.forEach(API.concatArrays([method.inputs, method.outputs]), function (object) {
                        enrichRepresentation(jsonSchema, object);
                    });
                });
            });
        }
        function enrichRepresentation(jsonSchema, representation) {
            var defs = jsonSchema ? jsonSchema["definitions"] : null;
            if (defs && representation) {
                var name = representation["name"];
                if (name) {
                    var foundDef = defs[name];
                    if (foundDef) {
                        if (angular.isArray(foundDef) && foundDef.length > 0) {
                            foundDef = foundDef[0];
                        }
                        log.info("Found def " + angular.toJson(foundDef) + " for name " + name);
                        representation["schema"] = foundDef;
                    }
                }
            }
        }
        function onWsdl(response) {
            $scope.services = [];
            var root = response.documentElement;
            var targetNamespace = root ? root.getAttribute("targetNamespace") : null;
            var name = root ? root.getAttribute("name") : null;
            var portTypes = response.getElementsByTagNameNS(wsdlNamespace, "portType");
            var services = response.getElementsByTagNameNS(wsdlNamespace, "service");
            var bindings = response.getElementsByTagNameNS(wsdlNamespace, "binding");
            angular.forEach(portTypes, function (portType) {
                var service = {
                    name: name,
                    targetNamespace: targetNamespace,
                    portName: portType.getAttribute("name") || "Unknown",
                    operations: []
                };
                $scope.services.push(service);
                var operations = portType.getElementsByTagNameNS(wsdlNamespace, "operation");
                angular.forEach(operations, function (operation) {
                    var input = operation.getElementsByTagNameNS(wsdlNamespace, "input");
                    var output = operation.getElementsByTagNameNS(wsdlNamespace, "output");
                    function createMessageData(data) {
                        var answer = [];
                        angular.forEach(data, function (item) {
                            var name = item.getAttribute("name");
                            if (name) {
                                answer.push({
                                    name: name
                                });
                            }
                        });
                        return answer;
                    }
                    var opData = {
                        name: operation.getAttribute("name") || "Unknown",
                        inputs: createMessageData(input),
                        outputs: createMessageData(output)
                    };
                    service.operations.push(opData);
                });
            });
            Core.$apply($scope);
        }
    }]);
})(API || (API = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.AboutController", ["$scope", "$location", "jolokia", "branding", "localStorage", function ($scope, $location, jolokia, branding, localStorage) {
        var log = Logger.get("About");
        $.ajax({
            url: "app/core/doc/about.md",
            dataType: 'html',
            cache: false,
            success: function (data, textStatus, jqXHR) {
                $scope.html = "Unable to download about.md";
                if (angular.isDefined(data)) {
                    $scope.html = marked(data);
                    $scope.branding = branding;
                    $scope.customBranding = branding.enabled;
                    try {
                        $scope.hawtioVersion = jolokia.request({
                            type: "read",
                            mbean: "hawtio:type=About",
                            attribute: "HawtioVersion"
                        }).value;
                    }
                    catch (Error) {
                        $scope.hawtioVersion = "N/A";
                    }
                    $scope.jolokiaVersion = jolokia.version().agent;
                    $scope.serverProduct = jolokia.version().info.product;
                    $scope.serverVendor = jolokia.version().info.vendor;
                    $scope.serverVersion = jolokia.version().info.version;
                }
                Core.$apply($scope);
            },
            error: function (jqXHR, textStatus, errorThrown) {
                $scope.html = "Unable to download about.md";
                Core.$apply($scope);
            }
        });
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    function parsePreferencesJson(value, key) {
        var answer = null;
        if (angular.isDefined(value)) {
            answer = Core.parseJsonText(value, "localStorage for " + key);
        }
        return answer;
    }
    Core.parsePreferencesJson = parsePreferencesJson;
    function configuredPluginsForPerspectiveId(perspectiveId, workspace, jolokia, localStorage) {
        var topLevelTabs = Perspective.topLevelTabsForPerspectiveId(workspace, perspectiveId);
        if (topLevelTabs && topLevelTabs.length > 0) {
            topLevelTabs = topLevelTabs.filter(function (tab) {
                var href = undefined;
                if (angular.isFunction(tab.href)) {
                    href = tab.href();
                }
                else if (angular.isString(tab.href)) {
                    href = tab.href;
                }
                return href && isValidFunction(workspace, tab.isValid, perspectiveId);
            });
            var id = "plugins-" + perspectiveId;
            var initPlugins = parsePreferencesJson(localStorage[id], id);
            if (initPlugins) {
                initPlugins = initPlugins.filter(function (p) {
                    return topLevelTabs.some(function (tab) { return tab.id === p.id; });
                });
                topLevelTabs.forEach(function (tab) {
                    var knownPlugin = initPlugins.some(function (p) { return p.id === tab.id; });
                    if (!knownPlugin) {
                        Core.log.info("Discovered new plugin in JVM since loading configuration: " + tab.id);
                        initPlugins.push({ id: tab.id, index: -1, displayName: tab.content, enabled: true, isDefault: false });
                    }
                });
            }
            else {
                initPlugins = topLevelTabs;
            }
        }
        var answer = safeTabsToPlugins(initPlugins);
        return answer;
    }
    Core.configuredPluginsForPerspectiveId = configuredPluginsForPerspectiveId;
    function safeTabsToPlugins(tabs) {
        var answer = [];
        if (tabs) {
            tabs.forEach(function (tab, idx) {
                var name;
                if (angular.isUndefined(tab.displayName)) {
                    name = tab.content;
                }
                else {
                    name = tab.displayName;
                }
                var enabled;
                if (angular.isUndefined(tab.enabled)) {
                    enabled = true;
                }
                else {
                    enabled = tab.enabled;
                }
                var isDefault;
                if (angular.isUndefined(tab.isDefault)) {
                    isDefault = false;
                }
                else {
                    isDefault = tab.isDefault;
                }
                answer.push({ id: tab.id, index: idx, displayName: name, enabled: enabled, isDefault: isDefault });
            });
        }
        return answer;
    }
    Core.safeTabsToPlugins = safeTabsToPlugins;
    function filterTopLevelTabs(perspective, workspace, configuredPlugins) {
        var topLevelTabs = Perspective.topLevelTabsForPerspectiveId(workspace, perspective);
        if (perspective === "website")
            return topLevelTabs;
        var result = [];
        configuredPlugins.forEach(function (p) {
            if (p.enabled) {
                var pid = p.id;
                var tab = null;
                if (pid) {
                    tab = topLevelTabs.find(function (t) { return t.id === pid; });
                }
                if (tab) {
                    result.push(tab);
                }
            }
        });
        return result;
    }
    Core.filterTopLevelTabs = filterTopLevelTabs;
    function initPreferenceScope($scope, localStorage, defaults) {
        angular.forEach(defaults, function (_default, key) {
            $scope[key] = _default['value'];
            var converter = _default['converter'];
            var formatter = _default['formatter'];
            if (!formatter) {
                formatter = function (value) {
                    return value;
                };
            }
            if (!converter) {
                converter = function (value) {
                    return value;
                };
            }
            if (key in localStorage) {
                var value = converter(localStorage[key]);
                Core.log.debug("from local storage, setting ", key, " to ", value);
                $scope[key] = value;
            }
            else {
                var value = _default['value'];
                Core.log.debug("from default, setting ", key, " to ", value);
                localStorage[key] = value;
            }
            var watchFunc = _default['override'];
            if (!watchFunc) {
                watchFunc = function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        if (angular.isFunction(_default['pre'])) {
                            _default.pre(newValue);
                        }
                        var value = formatter(newValue);
                        Core.log.debug("to local storage, setting ", key, " to ", value);
                        localStorage[key] = value;
                        if (angular.isFunction(_default['post'])) {
                            _default.post(newValue);
                        }
                    }
                };
            }
            if (_default['compareAsObject']) {
                $scope.$watch(key, watchFunc, true);
            }
            else {
                $scope.$watch(key, watchFunc);
            }
        });
    }
    Core.initPreferenceScope = initPreferenceScope;
    function isValidFunction(workspace, validFn, perspectiveId) {
        return !validFn || validFn(workspace, perspectiveId);
    }
    Core.isValidFunction = isValidFunction;
    function getDefaultPlugin(perspectiveId, workspace, jolokia, localStorage) {
        var plugins = Core.configuredPluginsForPerspectiveId(perspectiveId, workspace, jolokia, localStorage);
        var defaultPlugin = null;
        plugins.forEach(function (p) {
            if (p.isDefault) {
                defaultPlugin = p;
            }
        });
        return defaultPlugin;
    }
    Core.getDefaultPlugin = getDefaultPlugin;
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core.ConsoleController = Core._module.controller("Core.ConsoleController", ["$scope", "$element", "$templateCache", function ($scope, $element, $templateCache) {
        $scope.setHandler = function (clip) {
            clip.addEventListener('mouseDown', function (client, args) {
                var icon = $element.find('.icon-copy');
                var icon2 = $element.find('.icon-trash');
                if (this !== icon.get(0) && this !== icon2.get(0)) {
                    return;
                }
                if (this == icon.get(0)) {
                    copyToClipboard();
                }
                else {
                    clearLogs();
                    Core.notification('info', "Cleared logging console");
                }
                Core.$apply($scope);
            });
            function copyToClipboard() {
                var text = $templateCache.get("logClipboardTemplate").lines();
                text.removeAt(0);
                text.removeAt(text.length - 1);
                text.push('<ul>');
                $element.find('#log-panel-statements').children().each(function (index, child) {
                    text.push('  <li>' + child.innerHTML + '</li>');
                });
                text.push('</ul>');
                clip.setText(text.join('\n'));
            }
            function clearLogs() {
                $element.find('#log-panel-statements').children().remove();
            }
        };
    }]);
    Core.AppController = Core._module.controller("Core.AppController", ["$scope", "$location", "workspace", "jolokia", "jolokiaStatus", "$document", "pageTitle", "localStorage", "userDetails", "lastLocation", "jolokiaUrl", "branding", "ConnectOptions", "$timeout", "locationChangeStartTasks", "$route", function ($scope, $location, workspace, jolokia, jolokiaStatus, $document, pageTitle, localStorage, userDetails, lastLocation, jolokiaUrl, branding, ConnectOptions, $timeout, locationChangeStartTasks, $route) {
        $scope.collapse = '';
        $scope.match = null;
        $scope.pageTitle = [];
        $scope.userDetails = userDetails;
        $scope.confirmLogout = false;
        $scope.connectionFailed = false;
        $scope.connectFailure = {};
        $scope.showPrefs = false;
        $scope.logoClass = function () {
            if (branding.logoOnly) {
                return "without-text";
            }
            else {
                return "with-text";
            }
        };
        $scope.branding = branding;
        $scope.hasMBeans = function () { return workspace.hasMBeans(); };
        $scope.$watch('jolokiaStatus.xhr', function () {
            var failure = jolokiaStatus.xhr;
            $scope.connectionFailed = failure ? true : false;
            $scope.connectFailure.summaryMessage = null;
            if ($scope.connectionFailed) {
                $scope.connectFailure.status = failure.status;
                $scope.connectFailure.statusText = failure.statusText;
                var text = failure.responseText;
                if (text) {
                    try {
                        var html = $(text);
                        var markup = html.find("body");
                        if (markup && markup.length) {
                            html = markup;
                        }
                        html.each(function (idx, e) {
                            var name = e.localName;
                            if (name && name.startsWith("h")) {
                                $(e).addClass("ajaxError");
                            }
                        });
                        var container = $("<div></div>");
                        container.append(html);
                        $scope.connectFailure.summaryMessage = container.html();
                        console.log("Found HTML: " + $scope.connectFailure.summaryMessage);
                    }
                    catch (e) {
                        if (text.indexOf('<') < 0) {
                            $scope.connectFailure.summaryMessage = "<p>" + text + "</p>";
                        }
                    }
                }
            }
        });
        $scope.showPreferences = function () {
            $scope.showPrefs = true;
        };
        $scope.closePreferences = function () {
            $scope.showPrefs = false;
        };
        $scope.confirmConnectionFailed = function () {
            window.close();
        };
        $scope.setPageTitle = function () {
            $scope.pageTitle = pageTitle.getTitleArrayExcluding([branding.appName]);
            var tab = workspace.getActiveTab();
            if (tab && tab.content) {
                Core.setPageTitleWithTab($document, pageTitle, tab.content);
            }
            else {
                Core.setPageTitle($document, pageTitle);
            }
        };
        $scope.setRegexIndicator = function () {
            try {
                var regexs = angular.fromJson(localStorage['regexs']);
                if (regexs) {
                    regexs.reverse().each(function (regex) {
                        var r = new RegExp(regex.regex, 'g');
                        if (r.test($location.absUrl())) {
                            $scope.match = {
                                name: regex.name,
                                color: regex.color
                            };
                        }
                    });
                }
            }
            catch (e) {
            }
        };
        $scope.loggedIn = function () {
            return userDetails.username !== null && userDetails.username !== 'public';
        };
        $scope.showLogout = function () {
            return $scope.loggedIn() && angular.isDefined(userDetails.loginDetails);
        };
        $scope.logout = function () {
            $scope.confirmLogout = true;
        };
        $scope.getUsername = function () {
            if (userDetails.username && !userDetails.username.isBlank()) {
                return userDetails.username;
            }
            else {
                return 'user';
            }
        };
        $scope.doLogout = function () {
            $scope.confirmLogout = false;
            Core.logout(jolokiaUrl, userDetails, localStorage, $scope);
        };
        $scope.$watch(function () {
            return localStorage['regexs'];
        }, $scope.setRegexIndicator);
        $scope.reloaded = false;
        $scope.maybeRedirect = function () {
            if (userDetails.username === null) {
                var currentUrl = $location.url();
                if (!currentUrl.startsWith('/login')) {
                    lastLocation.url = currentUrl;
                    $location.url('/login');
                }
                else {
                    if (!$scope.reloaded) {
                        $route.reload();
                        $scope.reloaded = true;
                    }
                }
            }
            else {
                if ($location.url().startsWith('/login')) {
                    var url = defaultPage();
                    if (angular.isDefined(lastLocation.url)) {
                        url = lastLocation.url;
                    }
                    $location.url(url);
                }
            }
        };
        $scope.$watch('userDetails', function (newValue, oldValue) {
            $scope.maybeRedirect();
        }, true);
        $scope.$on('hawtioOpenPrefs', function () {
            $scope.showPrefs = true;
        });
        $scope.$on('hawtioClosePrefs', function () {
            $scope.showPrefs = false;
        });
        $scope.$on('$routeChangeStart', function (event, args) {
            if ((!args.params || !args.params.pref) && $scope.showPrefs) {
                $scope.showPrefs = false;
            }
            $scope.maybeRedirect();
        });
        $scope.$on('$routeChangeSuccess', function () {
            $scope.setPageTitle($document, Core.PageTitle);
            $scope.maybeRedirect();
        });
        $scope.fullScreen = function () {
            if ($location.path().startsWith("/login")) {
                return branding.fullscreenLogin;
            }
            var tab = $location.search()['tab'];
            if (tab) {
                return tab === "fullscreen";
            }
            return false;
        };
        $scope.login = function () {
            return $location.path().startsWith("/login");
        };
        function defaultPage() {
            return Perspective.defaultPage($location, workspace, jolokia, localStorage);
        }
    }]);
})(Core || (Core = {}));
$(function () {
    hawtioPluginLoader.loadPlugins(function () {
        var doc = angular.element(document);
        var docEl = angular.element(document.documentElement);
        Core.injector = angular.bootstrap(docEl, hawtioPluginLoader.getModules());
        Logger.get("Core").debug("Bootstrapped application, injector: ", Core.injector);
        docEl.attr('xmlns:ng', "http://angularjs.org");
        docEl.attr('ng-app', 'hawtioCore');
    });
});
var Core;
(function (Core) {
    Core._module.directive('noClick', function () {
        return function ($scope, $element, $attrs) {
            $element.click(function (event) {
                event.preventDefault();
            });
        };
    });
    Core._module.directive('logToggler', ["localStorage", function (localStorage) {
        return {
            restrict: 'A',
            link: function ($scope, $element, $attr) {
                $($element).click(function () {
                    var log = $("#log-panel");
                    var body = $('body');
                    if (log.height() !== 0) {
                        localStorage['showLog'] = 'false';
                        log.css({ 'bottom': '110%' });
                        body.css({
                            'overflow-y': 'auto'
                        });
                    }
                    else {
                        localStorage['showLog'] = 'true';
                        log.css({ 'bottom': '50%' });
                        body.css({
                            'overflow-y': 'hidden'
                        });
                    }
                    return false;
                });
            }
        };
    }]);
    Core._module.directive('autofill', ['$timeout', function ($timeout) {
        return {
            restrict: "A",
            require: 'ngModel',
            link: function (scope, elem, attrs, ctrl) {
                var ngModel = attrs["ngModel"];
                if (ngModel) {
                    var log = Logger.get("Core");
                    function checkForDifference() {
                        var modelValue = scope.$eval(ngModel);
                        var value = elem.val();
                        if (value && !modelValue) {
                            Core.pathSet(scope, ngModel, value);
                        }
                        else {
                            elem.trigger('input');
                            elem.trigger('change');
                            if (elem.length) {
                                var firstElem = $(elem[0]);
                                firstElem.trigger('input');
                                firstElem.trigger('change');
                            }
                        }
                    }
                    $timeout(checkForDifference, 200);
                    $timeout(checkForDifference, 800);
                    $timeout(checkForDifference, 1500);
                }
            }
        };
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.CorePreferences", ["$scope", "localStorage", function ($scope, localStorage) {
        Core.initPreferenceScope($scope, localStorage, {
            'updateRate': {
                'value': 5000,
                'post': function (newValue) {
                    $scope.$emit('UpdateRate', newValue);
                }
            },
            'showWelcomePage': {
                'value': true,
                'converter': Core.parseBooleanValue,
            },
            'regexs': {
                'value': "",
                'converter': function (value) {
                    if (angular.isArray(value)) {
                        return value;
                    }
                    else if (Core.isBlank(value)) {
                        return [];
                    }
                    return angular.fromJson(value);
                },
                'formatter': function (value) {
                    return angular.toJson(value);
                },
                'compareAsObject': true
            }
        });
        $scope.newHost = {};
        $scope.forms = {};
        $scope.addRegexDialog = new UI.Dialog();
        $scope.onOk = function (json, form) {
            $scope.addRegexDialog.close();
            json['color'] = UI.colors.sample();
            if (!angular.isArray($scope.regexs)) {
                $scope.regexs = [json];
            }
            else {
                $scope.regexs.push(json);
            }
            $scope.newHost = {};
            Core.$apply($scope);
        };
        $scope.hostSchema = {
            properties: {
                'name': {
                    description: 'Indicator name',
                    type: 'string',
                    required: true
                },
                'regex': {
                    description: 'Indicator regex',
                    type: 'string',
                    required: true
                }
            }
        };
        $scope.delete = function (index) {
            $scope.regexs.removeAt(index);
        };
        $scope.moveUp = function (index) {
            var tmp = $scope.regexs[index];
            $scope.regexs[index] = $scope.regexs[index - 1];
            $scope.regexs[index - 1] = tmp;
        };
        $scope.moveDown = function (index) {
            var tmp = $scope.regexs[index];
            $scope.regexs[index] = $scope.regexs[index + 1];
            $scope.regexs[index + 1] = tmp;
        };
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    var HelpRegistry = (function () {
        function HelpRegistry($rootScope) {
            this.$rootScope = $rootScope;
            this.discoverableDocTypes = {
                user: 'help.md'
            };
            this.topicNameMappings = {
                activemq: 'ActiveMQ',
                camel: 'Camel',
                jboss: 'JBoss',
                jclouds: 'jclouds',
                jmx: 'JMX',
                jvm: 'Connect',
                log: 'Logs',
                openejb: 'OpenEJB'
            };
            this.subTopicNameMappings = {
                user: 'For Users',
                developer: 'For Developers',
                faq: 'FAQ'
            };
            this.pluginNameMappings = {
                hawtioCore: 'core',
                'hawtio-branding': 'branding',
                forceGraph: 'forcegraph',
                'hawtio-ui': 'ui',
                'hawtio-forms': 'forms',
                elasticjs: 'elasticsearch'
            };
            this.ignoredPlugins = [
                'core',
                'branding',
                'datatable',
                'forcegraph',
                'forms',
                'perspective',
                'tree',
                'ui'
            ];
            this.topics = {};
        }
        HelpRegistry.prototype.addUserDoc = function (topic, path, isValid) {
            if (isValid === void 0) { isValid = null; }
            this.addSubTopic(topic, 'user', path, isValid);
        };
        HelpRegistry.prototype.addDevDoc = function (topic, path, isValid) {
            if (isValid === void 0) { isValid = null; }
            this.addSubTopic(topic, 'developer', path, isValid);
        };
        HelpRegistry.prototype.addSubTopic = function (topic, subtopic, path, isValid) {
            if (isValid === void 0) { isValid = null; }
            this.getOrCreateTopic(topic, isValid)[subtopic] = path;
        };
        HelpRegistry.prototype.getOrCreateTopic = function (topic, isValid) {
            if (isValid === void 0) { isValid = null; }
            if (!angular.isDefined(this.topics[topic])) {
                if (isValid === null) {
                    isValid = function () {
                        return true;
                    };
                }
                this.topics[topic] = {
                    isValid: isValid
                };
                this.$rootScope.$broadcast('hawtioNewHelpTopic');
            }
            return this.topics[topic];
        };
        HelpRegistry.prototype.mapTopicName = function (name) {
            if (angular.isDefined(this.topicNameMappings[name])) {
                return this.topicNameMappings[name];
            }
            return name.capitalize();
        };
        HelpRegistry.prototype.mapSubTopicName = function (name) {
            if (angular.isDefined(this.subTopicNameMappings[name])) {
                return this.subTopicNameMappings[name];
            }
            return name.capitalize();
        };
        HelpRegistry.prototype.getTopics = function () {
            var answer = {};
            angular.forEach(this.topics, function (value, key) {
                if (value.isValid()) {
                    Core.log.debug(key, " is available");
                    answer[key] = angular.fromJson(angular.toJson(value));
                }
                else {
                    Core.log.debug(key, " is not available");
                }
            });
            return answer;
        };
        HelpRegistry.prototype.disableAutodiscover = function (name) {
            this.ignoredPlugins.push(name);
        };
        HelpRegistry.prototype.discoverHelpFiles = function (plugins) {
            var self = this;
            console.log("Ignored plugins: ", self.ignoredPlugins);
            plugins.forEach(function (plugin) {
                var pluginName = self.pluginNameMappings[plugin];
                if (!angular.isDefined(pluginName)) {
                    pluginName = plugin;
                }
                if (!self.ignoredPlugins.any(function (p) {
                    return p === pluginName;
                })) {
                    angular.forEach(self.discoverableDocTypes, function (value, key) {
                        if (!angular.isDefined(self[pluginName]) || !angular.isDefined(self[pluginName][key])) {
                            var target = 'app/' + pluginName + '/doc/' + value;
                            console.log("checking: ", target);
                            $.ajax(target, {
                                type: 'HEAD',
                                statusCode: {
                                    200: function () {
                                        self.getOrCreateTopic(plugin)[key] = target;
                                    }
                                }
                            });
                        }
                    });
                }
            });
        };
        return HelpRegistry;
    })();
    Core.HelpRegistry = HelpRegistry;
})(Core || (Core = {}));
var Core;
(function (Core) {
    var PreferencesRegistry = (function () {
        function PreferencesRegistry() {
            this.tabs = {};
        }
        PreferencesRegistry.prototype.addTab = function (name, template, isValid) {
            if (isValid === void 0) { isValid = undefined; }
            if (!isValid) {
                isValid = function () {
                    return true;
                };
            }
            this.tabs[name] = {
                template: template,
                isValid: isValid
            };
        };
        PreferencesRegistry.prototype.getTab = function (name) {
            return this.tabs[name];
        };
        PreferencesRegistry.prototype.getTabs = function () {
            var answer = {};
            angular.forEach(this.tabs, function (value, key) {
                if (value.isValid()) {
                    answer[key] = value;
                }
            });
            return answer;
        };
        return PreferencesRegistry;
    })();
    Core.PreferencesRegistry = PreferencesRegistry;
    ;
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.factory('workspace', ["$location", "jmxTreeLazyLoadRegistry", "$compile", "$templateCache", "localStorage", "jolokia", "jolokiaStatus", "$rootScope", "userDetails", function ($location, jmxTreeLazyLoadRegistry, $compile, $templateCache, localStorage, jolokia, jolokiaStatus, $rootScope, userDetails) {
        var answer = new Core.Workspace(jolokia, jolokiaStatus, jmxTreeLazyLoadRegistry, $location, $compile, $templateCache, localStorage, $rootScope, userDetails);
        answer.loadTree();
        return answer;
    }]);
    Core._module.service('ConnectOptions', ['$location', function ($location) {
        var connectionName = Core.ConnectionName;
        if (!Core.isBlank(connectionName)) {
            var answer = Core.getConnectOptions(connectionName);
            Core.log.debug("ConnectOptions: ", answer);
            return answer;
        }
        Core.log.debug("No connection options, connected to local JVM");
        return null;
    }]);
    Core._module.service('localStorage', function () {
        return Core.getLocalStorage();
    });
    Core._module.factory('pageTitle', function () {
        var answer = new Core.PageTitle();
        return answer;
    });
    Core._module.factory('viewRegistry', function () {
        return {};
    });
    Core._module.factory('lastLocation', function () {
        return {};
    });
    Core._module.factory('locationChangeStartTasks', function () {
        return new Core.ParameterizedTasksImpl();
    });
    Core._module.factory('postLoginTasks', function () {
        return Core.postLoginTasks;
    });
    Core._module.factory('preLogoutTasks', function () {
        return Core.preLogoutTasks;
    });
    Core._module.factory('helpRegistry', ["$rootScope", function ($rootScope) {
        return new Core.HelpRegistry($rootScope);
    }]);
    Core._module.factory('preferencesRegistry', function () {
        return new Core.PreferencesRegistry();
    });
    Core._module.factory('toastr', ["$window", function ($window) {
        var answer = $window.toastr;
        if (!answer) {
            answer = {};
            $window.toaster = answer;
        }
        return answer;
    }]);
    Core._module.factory('metricsWatcher', ["$window", function ($window) {
        var answer = $window.metricsWatcher;
        if (!answer) {
            answer = {};
            $window.metricsWatcher = metricsWatcher;
        }
        return answer;
    }]);
    Core._module.factory('xml2json', function () {
        var jquery = $;
        return jquery.xml2json;
    });
    Core._module.factory('jolokiaUrl', function () {
        return Core.jolokiaUrl;
    });
    Core._module.factory('jolokiaStatus', function () {
        return {
            xhr: null
        };
    });
    Core.DEFAULT_MAX_DEPTH = 7;
    Core.DEFAULT_MAX_COLLECTION_SIZE = 500;
    Core._module.factory('jolokiaParams', ["jolokiaUrl", "localStorage", function (jolokiaUrl, localStorage) {
        var answer = {
            canonicalNaming: false,
            ignoreErrors: true,
            mimeType: 'application/json',
            maxDepth: Core.DEFAULT_MAX_DEPTH,
            maxCollectionSize: Core.DEFAULT_MAX_COLLECTION_SIZE
        };
        if ('jolokiaParams' in localStorage) {
            answer = angular.fromJson(localStorage['jolokiaParams']);
        }
        else {
            localStorage['jolokiaParams'] = angular.toJson(answer);
        }
        answer['url'] = jolokiaUrl;
        return answer;
    }]);
    Core._module.factory('branding', function () {
        var branding = Themes.brandings['hawtio'].setFunc({});
        branding.logoClass = function () {
            if (branding.logoOnly) {
                return "without-text";
            }
            else {
                return "with-text";
            }
        };
        return branding;
    });
    Core._module.factory('ResponseHistory', function () {
        var answer = Core.getResponseHistory();
        return answer;
    });
    Core._module.factory('userDetails', ["ConnectOptions", "localStorage", "$window", "$rootScope", function (ConnectOptions, localStorage, $window, $rootScope) {
        var answer = {
            username: null,
            password: null
        };
        if ('userDetails' in $window) {
            answer = $window['userDetails'];
            Core.log.debug("User details loaded from parent window: ", StringHelpers.toString(answer));
            Core.executePostLoginTasks();
        }
        else if ('userDetails' in localStorage) {
            answer = angular.fromJson(localStorage['userDetails']);
            Core.log.debug("User details loaded from local storage: ", StringHelpers.toString(answer));
            Core.executePostLoginTasks();
        }
        else if (Core.isChromeApp()) {
            answer = {
                username: 'user',
                password: ''
            };
            Core.log.debug("Running as a Chrome app, using fake UserDetails: ");
            Core.executePostLoginTasks();
        }
        else {
            Core.log.debug("No username set, checking if we have a session");
            var userUrl = "user";
            $.ajax(userUrl, {
                type: "GET",
                success: function (response) {
                    Core.log.debug("Got user response: ", response);
                    if (response === null) {
                        answer.username = null;
                        answer.password = null;
                        Core.log.debug("user response was null, no session available");
                        Core.$apply($rootScope);
                        return;
                    }
                    answer.username = response;
                    if (response === 'user') {
                        Core.log.debug("Authentication disabled, using dummy credentials");
                        answer.loginDetails = {};
                    }
                    else {
                        Core.log.debug("User details loaded from existing session: ", StringHelpers.toString(answer));
                    }
                    Core.executePostLoginTasks();
                    Core.$apply($rootScope);
                },
                error: function (xhr, textStatus, error) {
                    answer.username = null;
                    answer.password = null;
                    Core.log.debug("Failed to get session username: ", error);
                    Core.$apply($rootScope);
                }
            });
            Core.log.debug("Created empty user details to be filled in: ", StringHelpers.toString(answer));
        }
        return answer;
    }]);
    Core._module.factory('jmxTreeLazyLoadRegistry', function () {
        return Core.lazyLoaders;
    });
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core.fileUploadMBean = "hawtio:type=UploadManager";
    var FileUpload = (function () {
        function FileUpload() {
            this.restrict = 'A';
            this.replace = true;
            this.templateUrl = Core.templatePath + "fileUpload.html";
            this.scope = {
                files: '=hawtioFileUpload',
                target: '@',
                showFiles: '@'
            };
            this.controller = ["$scope", "$element", "$attrs", "jolokia", function ($scope, $element, $attrs, jolokia) {
                $scope.target = '';
                $scope.response = '';
                $scope.percentComplete = 0;
                UI.observe($scope, $attrs, 'target', '');
                UI.observe($scope, $attrs, 'showFiles', true);
                $scope.update = function (response) {
                    var responseJson = angular.toJson(response.value);
                    if ($scope.responseJson !== responseJson) {
                        $scope.responseJson = responseJson;
                        $scope.files = response.value;
                        Core.$applyNowOrLater($scope);
                    }
                };
                $scope.delete = function (fileName) {
                    jolokia.request({
                        type: 'exec',
                        mbean: Core.fileUploadMBean,
                        operation: 'delete(java.lang.String, java.lang.String)',
                        arguments: [$scope.target, fileName]
                    }, {
                        success: function () {
                            Core.$apply($scope);
                        },
                        error: function (response) {
                            Core.notification('error', "Failed to delete " + fileName + " due to: " + response.error);
                            Core.$apply($scope);
                        }
                    });
                };
                $scope.$watch('target', function (newValue, oldValue) {
                    if (oldValue !== newValue) {
                        Core.unregister(jolokia, $scope);
                    }
                    Core.register(jolokia, $scope, {
                        type: 'exec',
                        mbean: Core.fileUploadMBean,
                        operation: 'list(java.lang.String)',
                        arguments: [$scope.target]
                    }, onSuccess($scope.update));
                });
            }];
            this.link = function ($scope, $element, $attrs) {
                var fileInput = $element.find('input[type=file]');
                var form = $element.find('form[name=file-upload]');
                var button = $element.find('input[type=button]');
                var onFileChange = function () {
                    button.prop('disabled', true);
                    var files = fileInput.get(0).files;
                    var fileName = files.length + " files";
                    if (files.length === 1) {
                        fileName = files[0].name;
                    }
                    form.ajaxSubmit({
                        beforeSubmit: function (arr, $form, options) {
                            Core.notification('info', "Uploading " + fileName);
                            $scope.percentComplete = 0;
                            Core.$apply($scope);
                        },
                        success: function (response, statusText, xhr, $form) {
                            Core.notification('success', "Uploaded " + fileName);
                            setTimeout(function () {
                                button.prop('disabled', false);
                                $scope.percentComplete = 0;
                                Core.$apply($scope);
                            }, 1000);
                            Core.$apply($scope);
                        },
                        error: function (response, statusText, xhr, $form) {
                            Core.notification('error', "Failed to upload " + fileName + " due to " + statusText);
                            setTimeout(function () {
                                button.prop('disabled', false);
                                $scope.percentComplete = 0;
                                Core.$apply($scope);
                            }, 1000);
                            Core.$apply($scope);
                        },
                        uploadProgress: function (event, position, total, percentComplete) {
                            $scope.percentComplete = percentComplete;
                            Core.$apply($scope);
                        }
                    });
                    return false;
                };
                button.click(function () {
                    if (!button.prop('disabled')) {
                        fileInput.click();
                    }
                    return false;
                });
                form.submit(function () {
                    return false;
                });
                if ($.browser.msie) {
                    fileInput.click(function (event) {
                        setTimeout(function () {
                            if (fileInput.val().length > 0) {
                                onFileChange();
                            }
                        }, 0);
                    });
                }
                else {
                    fileInput.change(onFileChange);
                }
            };
        }
        return FileUpload;
    })();
    Core.FileUpload = FileUpload;
    Core._module.directive('hawtioFileUpload', function () {
        return new Core.FileUpload();
    });
})(Core || (Core = {}));
var Core;
(function (Core) {
    var GridStyle = (function () {
        function GridStyle($window) {
            var _this = this;
            this.$window = $window;
            this.restrict = 'C';
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        GridStyle.prototype.doLink = function (scope, element, attrs) {
            var lastHeight = 0;
            var resizeFunc = angular.bind(this, function (scope) {
                var top = element.position().top;
                var windowHeight = $(this.$window).height();
                var height = windowHeight - top - 15;
                var heightStr = height + 'px';
                element.css({
                    'min-height': heightStr,
                    'height': heightStr
                });
                if (lastHeight !== height) {
                    lastHeight = height;
                    element.trigger('resize');
                }
            });
            resizeFunc();
            scope.$watch(resizeFunc);
            $(this.$window).resize(function () {
                resizeFunc();
                Core.$apply(scope);
                return false;
            });
        };
        return GridStyle;
    })();
    Core.GridStyle = GridStyle;
    Core._module.directive('gridStyle', ["$window", function ($window) {
        return new Core.GridStyle($window);
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.HelpController", ["$scope", "$routeParams", "marked", "helpRegistry", "branding", function ($scope, $routeParams, marked, helpRegistry, branding) {
        $scope.branding = branding;
        $scope.topics = helpRegistry.getTopics();
        if ('topic' in $routeParams) {
            $scope.topic = $routeParams['topic'];
        }
        else {
            $scope.topic = 'index';
        }
        if ('subtopic' in $routeParams) {
            $scope.subTopic = $routeParams['subtopic'];
        }
        else {
            $scope.subTopic = Object.extended($scope.topics[$scope.topic]).keys().first();
        }
        Core.log.debug("topic: ", $scope.topic, " subtopic: ", $scope.subTopic);
        var isIndex = $scope.topic === "index";
        var filterSubTopic = $scope.subTopic;
        if (isIndex && filterSubTopic !== "developer") {
            filterSubTopic = "user";
        }
        $scope.breadcrumbs = [
            {
                topic: "index",
                subTopic: "user",
                label: "User Guide"
            },
            {
                topic: "index",
                subTopic: "faq",
                label: "FAQ"
            },
            {
                topic: "index",
                subTopic: "changes",
                label: "Changes"
            },
            {
                topic: "index",
                subTopic: "developer",
                label: "Developers"
            }
        ];
        $scope.sectionLink = function (section) {
            var topic = section.topic || "";
            var subTopic = section.subTopic || "";
            var link = Core.pathGet(helpRegistry.topics, [topic, subTopic]);
            if (link && link.indexOf("#") >= 0) {
                return link;
            }
            else {
                return "#/help/" + topic + "/" + subTopic;
            }
        };
        var activeBreadcrumb = $scope.breadcrumbs.find(function (b) { return b.topic === $scope.topic && b.subTopic === $scope.subTopic; });
        if (activeBreadcrumb)
            activeBreadcrumb.active = true;
        $scope.sections = [];
        angular.forEach($scope.topics, function (details, topic) {
            if (topic !== "index" && details[filterSubTopic]) {
                $scope.sections.push({
                    topic: topic,
                    subTopic: filterSubTopic,
                    label: helpRegistry.mapTopicName(topic),
                    active: topic === $scope.topic
                });
            }
        });
        $scope.sections = $scope.sections.sortBy("label");
        $scope.$on('hawtioNewHelpTopic', function () {
            $scope.topics = helpRegistry.getTopics();
        });
        $scope.$watch('topics', function (newValue, oldValue) {
            Core.log.debug("Topics: ", $scope.topics);
        });
        if (!angular.isDefined($scope.topics[$scope.topic])) {
            $scope.html = "Unable to download help data for " + $scope.topic;
        }
        else {
            $.ajax({
                url: $scope.topics[$scope.topic][$scope.subTopic],
                dataType: 'html',
                cache: false,
                success: function (data, textStatus, jqXHR) {
                    $scope.html = "Unable to download help data for " + $scope.topic;
                    if (angular.isDefined(data)) {
                        $scope.html = marked(data);
                    }
                    Core.$apply($scope);
                },
                error: function (jqXHR, textStatus, errorThrown) {
                    $scope.html = "Unable to download help data for " + $scope.topic;
                    Core.$apply($scope);
                }
            });
        }
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.JolokiaPreferences", ["$scope", "localStorage", "jolokiaParams", "$window", function ($scope, localStorage, jolokiaParams, $window) {
        Core.initPreferenceScope($scope, localStorage, {
            'maxDepth': {
                'value': Core.DEFAULT_MAX_DEPTH,
                'converter': parseInt,
                'formatter': parseInt,
                'post': function (newValue) {
                    jolokiaParams.maxDepth = newValue;
                    localStorage['jolokiaParams'] = angular.toJson(jolokiaParams);
                }
            },
            'maxCollectionSize': {
                'value': Core.DEFAULT_MAX_COLLECTION_SIZE,
                'converter': parseInt,
                'formatter': parseInt,
                'post': function (newValue) {
                    jolokiaParams.maxCollectionSize = newValue;
                    localStorage['jolokiaParams'] = angular.toJson(jolokiaParams);
                }
            }
        });
        $scope.reboot = function () {
            $window.location.reload();
        };
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.factory('jolokia', ["$location", "localStorage", "jolokiaStatus", "$rootScope", "userDetails", "jolokiaParams", "jolokiaUrl", function ($location, localStorage, jolokiaStatus, $rootScope, userDetails, jolokiaParams, jolokiaUrl) {
        Core.log.debug("Jolokia URL is " + jolokiaUrl);
        if (jolokiaUrl) {
            var connectionName = Core.getConnectionNameParameter($location.search());
            var connectionOptions = Core.getConnectOptions(connectionName);
            var username = null;
            var password = null;
            var found = false;
            try {
                if (window.opener && "passUserDetails" in window.opener) {
                    username = window.opener["passUserDetails"].username;
                    password = window.opener["passUserDetails"].password;
                    found = true;
                }
            }
            catch (securityException) {
            }
            if (!found) {
                if (connectionOptions && connectionOptions.userName && connectionOptions.password) {
                    username = connectionOptions.userName;
                    password = connectionOptions.password;
                }
                else if (angular.isDefined(userDetails) && angular.isDefined(userDetails.username) && angular.isDefined(userDetails.password)) {
                    username = userDetails.username;
                    password = userDetails.password;
                }
                else {
                    var search = hawtioPluginLoader.parseQueryString();
                    username = search["_user"];
                    password = search["_pwd"];
                    if (angular.isArray(username))
                        username = username[0];
                    if (angular.isArray(password))
                        password = password[0];
                }
            }
            if (username && password) {
                userDetails.username = username;
                userDetails.password = password;
                $.ajaxSetup({
                    beforeSend: function (xhr) {
                        xhr.setRequestHeader('Authorization', Core.getBasicAuthHeader(userDetails.username, userDetails.password));
                    }
                });
                var loginUrl = jolokiaUrl.replace("jolokia", "auth/login/");
                $.ajax(loginUrl, {
                    type: "POST",
                    success: function (response) {
                        if (response['credentials'] || response['principals']) {
                            userDetails.loginDetails = {
                                'credentials': response['credentials'],
                                'principals': response['principals']
                            };
                        }
                        else {
                            var doc = Core.pathGet(response, ['children', 0, 'innerHTML']);
                            if (doc) {
                                Core.log.debug("Response is a document (ignoring this): ", doc);
                            }
                        }
                        Core.executePostLoginTasks();
                    },
                    error: function (xhr, textStatus, error) {
                        Core.executePostLoginTasks();
                    }
                });
            }
            jolokiaParams['ajaxError'] = function (xhr, textStatus, error) {
                if (xhr.status === 401 || xhr.status === 403) {
                    userDetails.username = null;
                    userDetails.password = null;
                    delete userDetails.loginDetails;
                    if (found) {
                        delete window.opener["passUserDetails"];
                    }
                }
                else {
                    jolokiaStatus.xhr = xhr;
                    if (!xhr.responseText && error) {
                        xhr.responseText = error.stack;
                    }
                }
                Core.$apply($rootScope);
            };
            var jolokia = new Jolokia(jolokiaParams);
            localStorage['url'] = jolokiaUrl;
            jolokia.stop();
            return jolokia;
        }
        else {
            var answer = {
                running: false,
                request: function (req, opts) { return null; },
                register: function (req, opts) { return null; },
                list: function (path, opts) { return null; },
                search: function (mBeanPatter, opts) { return null; },
                getAttribute: function (mbean, attribute, path, opts) { return null; },
                setAttribute: function (mbean, attribute, value, path, opts) {
                },
                version: function (opts) { return null; },
                execute: function (mbean, operation) {
                    var args = [];
                    for (var _i = 2; _i < arguments.length; _i++) {
                        args[_i - 2] = arguments[_i];
                    }
                    return null;
                },
                start: function (period) {
                    answer.running = true;
                },
                stop: function () {
                    answer.running = false;
                },
                isRunning: function () { return answer.running; },
                jobs: function () { return []; }
            };
            return answer;
        }
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.LoggingPreferences", ["$scope", function ($scope) {
        Core.initPreferenceScope($scope, localStorage, {
            'logBuffer': {
                'value': 100,
                'converter': parseInt,
                'formatter': parseInt,
                'post': function (newValue) {
                    window['LogBuffer'] = newValue;
                }
            },
            'logLevel': {
                'value': '{"value": 2, "name": "INFO"}',
                'post': function (value) {
                    var level = angular.fromJson(value);
                    Logger.setLevel(level);
                }
            }
        });
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core.NavBarController = Core._module.controller("Core.NavBarController", ["$scope", "$location", "workspace", "$route", "jolokia", "localStorage", "NavBarViewCustomLinks", function ($scope, $location, workspace, $route, jolokia, localStorage, NavBarViewCustomLinks) {
        $scope.hash = workspace.hash();
        $scope.topLevelTabs = [];
        $scope.subLevelTabs = workspace.subLevelTabs;
        $scope.currentPerspective = null;
        $scope.localStorage = localStorage;
        $scope.recentConnections = [];
        $scope.goTo = function (destination) {
            $location.url(destination);
        };
        $scope.$watch('localStorage.recentConnections', function (newValue, oldValue) {
            $scope.recentConnections = Core.getRecentConnections(localStorage);
        });
        $scope.openConnection = function (connection) {
            var connectOptions = Core.getConnectOptions(connection);
            if (connectOptions) {
                Core.connectToServer(localStorage, connectOptions);
            }
        };
        $scope.goHome = function () {
            window.open(".");
        };
        $scope.clearConnections = Core.clearConnections;
        $scope.perspectiveDetails = {
            perspective: null
        };
        $scope.topLevelTabs = function () {
            reloadPerspective();
            return workspace.topLevelTabs;
        };
        $scope.$on('jmxTreeUpdated', function () {
            reloadPerspective();
        });
        $scope.$watch('workspace.topLevelTabs', function () {
            reloadPerspective();
        });
        $scope.validSelection = function (uri) { return workspace.validSelection(uri); };
        $scope.isValid = function (nav) { return nav && nav.isValid(workspace); };
        $scope.switchPerspective = function (perspective) {
            if (perspective.onSelect && angular.isFunction(perspective.onSelect)) {
                perspective.onSelect.apply();
                return;
            }
            var searchPerspectiveId = $location.search()[Perspective.perspectiveSearchId];
            if (perspective && ($scope.currentPerspective !== perspective || perspective.id !== searchPerspectiveId)) {
                Logger.debug("Changed the perspective to " + JSON.stringify(perspective) + " from search id " + searchPerspectiveId);
                if ($scope.currentPerspective) {
                    $scope.currentPerspective.lastPage = $location.url();
                }
                var pid = perspective.id;
                $location.search(Perspective.perspectiveSearchId, pid);
                Logger.debug("Setting perspective to " + pid);
                $scope.currentPerspective = perspective;
                reloadPerspective();
                $scope.topLevelTabs = Perspective.getTopLevelTabsForPerspective($location, workspace, jolokia, localStorage);
                var defaultPlugin = Core.getDefaultPlugin(pid, workspace, jolokia, localStorage);
                var defaultTab;
                var path;
                if (defaultPlugin) {
                    $scope.topLevelTabs.forEach(function (tab) {
                        if (tab.id === defaultPlugin.id) {
                            defaultTab = tab;
                        }
                    });
                    if (defaultTab) {
                        path = Core.trimLeading(defaultTab.href(), "#");
                    }
                }
                else {
                    if (perspective.lastPage) {
                        path = Core.trimLeading(perspective.lastPage, "#");
                    }
                }
                if (path) {
                    var idx = path.indexOf("?p=") || path.indexOf("&p=");
                    if (idx > 0) {
                        path = path.substring(0, idx);
                    }
                    var sep = (path.indexOf("?") >= 0) ? "&" : "?";
                    path += sep + "p=" + pid;
                    $location.url(path);
                }
            }
        };
        $scope.$watch('hash', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                Core.log.debug("hash changed from ", oldValue, " to ", newValue);
            }
        });
        $scope.$on('$routeChangeSuccess', function () {
            $scope.hash = workspace.hash();
            reloadPerspective();
        });
        $scope.link = function (nav, includePerspective) {
            if (includePerspective === void 0) { includePerspective = false; }
            var href;
            if (angular.isString(nav)) {
                href = nav;
            }
            else {
                href = angular.isObject(nav) ? nav.href() : null;
            }
            href = href || "";
            var removeParams = ['tab', 'nid', 'chapter', 'pref', 'q'];
            if (!includePerspective && href) {
                if (href.indexOf("?p=") >= 0 || href.indexOf("&p=") >= 0) {
                    removeParams.push("p");
                }
            }
            return Core.createHref($location, href, removeParams);
        };
        $scope.fullScreenLink = function () {
            var href = "#" + $location.path() + "?tab=notree";
            return Core.createHref($location, href, ['tab']);
        };
        $scope.addToDashboardLink = function () {
            var href = "#" + $location.path() + workspace.hash();
            var answer = "#/dashboard/add?tab=dashboard&href=" + encodeURIComponent(href);
            if ($location.url().has("/jmx/charts")) {
                var size = {
                    size_x: 4,
                    size_y: 3
                };
                answer += "&size=" + encodeURIComponent(angular.toJson(size));
            }
            return answer;
        };
        $scope.isActive = function (nav) {
            if (angular.isString(nav))
                return workspace.isLinkActive(nav);
            var fn = nav.isActive;
            if (fn) {
                return fn(workspace);
            }
            return workspace.isLinkActive(nav.href());
        };
        $scope.isTopTabActive = function (nav) {
            if (angular.isString(nav))
                return workspace.isTopTabActive(nav);
            var fn = nav.isActive;
            if (fn) {
                return fn(workspace);
            }
            return workspace.isTopTabActive(nav.href());
        };
        $scope.activeLink = function () {
            var tabs = $scope.topLevelTabs();
            if (!tabs) {
                return "Loading...";
            }
            var tab = tabs.find(function (nav) {
                return $scope.isActive(nav);
            });
            return tab ? tab['content'] : "";
        };
        $scope.navBarViewCustomLinks = NavBarViewCustomLinks;
        $scope.isCustomLinkSet = function () {
            return $scope.navBarViewCustomLinks.list.length;
        };
        function reloadPerspective() {
            var perspectives = Perspective.getPerspectives($location, workspace, jolokia, localStorage);
            var currentId = Perspective.currentPerspectiveId($location, workspace, jolokia, localStorage);
            var newTopLevelTabs = Perspective.getTopLevelTabsForPerspective($location, workspace, jolokia, localStorage);
            var diff = newTopLevelTabs.subtract($scope.topLevelTabs);
            if (diff && diff.length > 0) {
                $scope.topLevelTabs = newTopLevelTabs;
                $scope.perspectiveId = currentId;
                $scope.perspectives = perspectives;
                $scope.perspectiveDetails.perspective = $scope.perspectives.find(function (p) {
                    return p['id'] === currentId;
                });
                Core.$apply($scope);
            }
        }
    }]);
    Core._module.service("NavBarViewCustomLinks", ['$location', '$rootScope', function ($location, $rootScope) {
        return {
            list: [],
            dropDownLabel: "Extra"
        };
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core.PluginPreferences = Core._module.controller("Core.PluginPreferences", ["$scope", "localStorage", "$location", "workspace", "jolokia", function ($scope, localStorage, $location, workspace, jolokia) {
        Core.initPreferenceScope($scope, localStorage, {
            'autoRefresh': {
                'value': true,
                'converter': Core.parseBooleanValue
            }
        });
        $scope.perspectiveId = null;
        $scope.perspectives = [];
        $scope.plugins = [];
        $scope.pluginDirty = false;
        $scope.pluginMoveUp = function (index) {
            $scope.pluginDirty = true;
            var tmp = $scope.plugins[index];
            $scope.plugins[index] = $scope.plugins[index - 1];
            $scope.plugins[index - 1] = tmp;
        };
        $scope.pluginMoveDown = function (index) {
            $scope.pluginDirty = true;
            var tmp = $scope.plugins[index];
            $scope.plugins[index] = $scope.plugins[index + 1];
            $scope.plugins[index + 1] = tmp;
        };
        $scope.pluginDisable = function (index) {
            $scope.pluginDirty = true;
            var atLeastOneEnabled = false;
            $scope.plugins.forEach(function (p, idx) {
                if (idx != index && p.enabled) {
                    atLeastOneEnabled = true;
                }
            });
            if (atLeastOneEnabled) {
                $scope.plugins[index].enabled = false;
                $scope.plugins[index].isDefault = false;
            }
        };
        $scope.pluginEnable = function (index) {
            $scope.pluginDirty = true;
            $scope.plugins[index].enabled = true;
        };
        $scope.pluginDefault = function (index) {
            $scope.pluginDirty = true;
            $scope.plugins.forEach(function (p) {
                p.isDefault = false;
            });
            $scope.plugins[index].isDefault = true;
            $scope.plugins[index].enabled = true;
        };
        $scope.pluginApply = function () {
            $scope.pluginDirty = false;
            var noDefault = true;
            $scope.plugins.forEach(function (p, idx) {
                if (p.isDefault) {
                    noDefault = false;
                }
                p.index = idx;
            });
            if (noDefault) {
                $scope.plugins.find(function (p) {
                    return p.enabled == true;
                }).isDefault = true;
            }
            var json = angular.toJson($scope.plugins);
            if (json) {
                Core.log.debug("Saving plugin settings for perspective " + $scope.perspectiveId + " -> " + json);
                var id = "plugins-" + $scope.perspectiveId;
                localStorage[id] = json;
            }
            setTimeout(function () {
                window.location.hash = "#";
            }, 10);
        };
        $scope.$watch('perspectiveId', function (newValue, oldValue) {
            if (newValue === oldValue) {
                return;
            }
            var perspective = Perspective.getPerspectiveById(newValue);
            if (perspective) {
                updateToPerspective(perspective);
                Core.$apply($scope);
            }
        });
        function updateToPerspective(perspective) {
            var plugins = Core.configuredPluginsForPerspectiveId(perspective.id, workspace, jolokia, localStorage);
            $scope.plugins = plugins;
            $scope.perspectiveId = perspective.id;
            Core.log.debug("Updated to perspective " + $scope.perspectiveId + " with " + plugins.length + " plugins");
        }
        $scope.perspectives = Perspective.getPerspectives($location, workspace, jolokia, localStorage);
        Core.log.debug("There are " + $scope.perspectives.length + " perspectives");
        var selectPerspective;
        var perspectiveId = Perspective.currentPerspectiveId($location, workspace, jolokia, localStorage);
        if (perspectiveId) {
            selectPerspective = $scope.perspectives.find(function (p) { return p.id === perspectiveId; });
        }
        if (!selectPerspective) {
            selectPerspective = $scope.perspectives[0];
        }
        updateToPerspective(selectPerspective);
        Core.$apply($scope);
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.PreferencesController", ["$scope", "$location", "workspace", "preferencesRegistry", "$element", function ($scope, $location, workspace, preferencesRegistry, $element) {
        Core.bindModelToSearchParam($scope, $location, "pref", "pref", "Core");
        $scope.panels = {};
        $scope.$watch(function () {
            return $element.is(':visible');
        }, function (newValue, oldValue) {
            if (newValue) {
                setTimeout(function () {
                    $scope.panels = preferencesRegistry.getTabs();
                    Core.log.debug("Panels: ", $scope.panels);
                    Core.$apply($scope);
                }, 50);
            }
        });
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.ResetPreferences", ["$scope", "userDetails", "jolokiaUrl", "localStorage", function ($scope, userDetails, jolokiaUrl, localStorage) {
        $scope.doReset = function () {
            Core.log.info("Resetting");
            var doReset = function () {
                localStorage.clear();
                setTimeout(function () {
                    window.location.reload();
                }, 10);
            };
            if (Core.isBlank(userDetails.username) && Core.isBlank(userDetails.password)) {
                doReset();
            }
            else {
                Core.logout(jolokiaUrl, userDetails, localStorage, $scope, doReset);
            }
        };
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core.ViewController = Core._module.controller("Core.ViewController", ["$scope", "$route", "$location", "layoutTree", "layoutFull", "viewRegistry", function ($scope, $route, $location, layoutTree, layoutFull, viewRegistry) {
        findViewPartial();
        $scope.$on("$routeChangeSuccess", function (event, current, previous) {
            findViewPartial();
        });
        function searchRegistry(path) {
            var answer = undefined;
            Object.extended(viewRegistry).keys(function (key, value) {
                if (!answer) {
                    if (key.startsWith("/") && key.endsWith("/")) {
                        var text = key.substring(1, key.length - 1);
                        try {
                            var reg = new RegExp(text, "");
                            if (reg.exec(path)) {
                                answer = value;
                            }
                        }
                        catch (e) {
                            Core.log.debug("Invalid RegExp " + text + " for viewRegistry value: " + value);
                        }
                    }
                    else {
                        if (path.startsWith(key)) {
                            answer = value;
                        }
                    }
                }
            });
            return answer;
        }
        function findViewPartial() {
            var answer = null;
            var hash = $location.search();
            var tab = hash['tab'];
            if (angular.isString(tab)) {
                answer = searchRegistry(tab);
            }
            if (!answer) {
                var path = $location.path();
                if (path) {
                    if (path.startsWith("/")) {
                        path = path.substring(1);
                    }
                    answer = searchRegistry(path);
                }
            }
            if (!answer) {
                answer = layoutTree;
            }
            $scope.viewPartial = answer;
            Core.log.debug("Using view partial: " + answer);
            return answer;
        }
    }]);
})(Core || (Core = {}));
var Core;
(function (Core) {
    Core._module.controller("Core.WelcomeController", ["$scope", "$location", "branding", "localStorage", function ($scope, $location, branding, localStorage) {
        $scope.branding = branding;
        var log = Logger.get("Welcome");
        $scope.stopShowingWelcomePage = function () {
            log.debug("Stop showing welcome page");
            localStorage['showWelcomePage'] = false;
            $location.path("/");
        };
        $scope.$watch('branding.welcomePageUrl', function (newValue, oldValue) {
            $.ajax({
                url: branding.welcomePageUrl,
                dataType: 'html',
                cache: false,
                success: function (data, textStatus, jqXHR) {
                    $scope.html = "Unable to download welcome.md";
                    if (angular.isDefined(data)) {
                        $scope.html = branding.onWelcomePage(data);
                    }
                    Core.$apply($scope);
                },
                error: function (jqXHR, textStatus, errorThrown) {
                    $scope.html = "Unable to download welcome.md";
                    Core.$apply($scope);
                }
            });
        });
    }]);
})(Core || (Core = {}));
var DataTable;
(function (DataTable) {
    var TableWidget = (function () {
        function TableWidget(scope, $templateCache, $compile, dataTableColumns, config) {
            var _this = this;
            if (config === void 0) { config = {}; }
            this.scope = scope;
            this.$templateCache = $templateCache;
            this.$compile = $compile;
            this.dataTableColumns = dataTableColumns;
            this.config = config;
            this.ignoreColumnHash = {};
            this.flattenColumnHash = {};
            this.detailTemplate = null;
            this.openMessages = [];
            this.addedExpandNodes = false;
            this.tableElement = null;
            this.sortColumns = null;
            this.dataTableConfig = {
                bPaginate: false,
                sDom: 'Rlfrtip',
                bDestroy: true,
                bAutoWidth: true
            };
            this.dataTable = null;
            angular.forEach(config.ignoreColumns, function (name) {
                _this.ignoreColumnHash[name] = true;
            });
            angular.forEach(config.flattenColumns, function (name) {
                _this.flattenColumnHash[name] = true;
            });
            var templateId = config.rowDetailTemplateId;
            if (templateId) {
                this.detailTemplate = this.$templateCache.get(templateId);
            }
        }
        TableWidget.prototype.addData = function (newData) {
            var dataTable = this.dataTable;
            dataTable.fnAddData(newData);
        };
        TableWidget.prototype.populateTable = function (data) {
            var _this = this;
            var $scope = this.scope;
            if (!data) {
                $scope.messages = [];
            }
            else {
                $scope.messages = data;
                var formatMessageDetails = function (dataTable, parentRow) {
                    var oData = dataTable.fnGetData(parentRow);
                    var div = $('<div>');
                    div.addClass('innerDetails');
                    _this.populateDetailDiv(oData, div);
                    return div;
                };
                var array = data;
                if (angular.isArray(data)) {
                }
                else if (angular.isObject(data)) {
                    array = [];
                    angular.forEach(data, function (object) { return array.push(object); });
                }
                var tableElement = this.tableElement;
                if (!tableElement) {
                    tableElement = $('#grid');
                }
                var tableTr = Core.getOrCreateElements(tableElement, ["thead", "tr"]);
                var tableBody = Core.getOrCreateElements(tableElement, ["tbody"]);
                var ths = $(tableTr).find("th");
                var columns = [];
                angular.forEach(this.dataTableColumns, function (value) { return columns.push(value); });
                var addColumn = function (key, title) {
                    columns.push({
                        "sDefaultContent": "",
                        "mData": null,
                        mDataProp: key
                    });
                    if (tableTr) {
                        $("<th>" + title + "</th>").appendTo(tableTr);
                    }
                };
                var checkForNewColumn = function (value, key, prefix) {
                    var found = _this.ignoreColumnHash[key] || columns.any(function (k, v) { return "mDataProp" === k && v === key; });
                    if (!found) {
                        if (_this.flattenColumnHash[key]) {
                            if (angular.isObject(value)) {
                                var childPrefix = prefix + key + ".";
                                angular.forEach(value, function (value, key) { return checkForNewColumn(value, key, childPrefix); });
                            }
                        }
                        else {
                            addColumn(prefix + key, Core.humanizeValue(key));
                        }
                    }
                };
                if (!this.config.disableAddColumns && angular.isArray(array) && array.length > 0) {
                    var first = array[0];
                    if (angular.isObject(first)) {
                        angular.forEach(first, function (value, key) { return checkForNewColumn(value, key, ""); });
                    }
                }
                if (columns.length > 1) {
                    var col0 = columns[0];
                    if (!this.sortColumns && !col0["mDataProp"] && !col0["mData"]) {
                        var sortOrder = [[1, "asc"]];
                        this.sortColumns = sortOrder;
                    }
                }
                if (array.length && !angular.isArray(array[0])) {
                    this.dataTableConfig["aaData"] = array;
                }
                else {
                    this.dataTableConfig["aaData"] = array;
                }
                this.dataTableConfig["aoColumns"] = columns;
                if (this.sortColumns) {
                    this.dataTableConfig["aaSorting"] = this.sortColumns;
                }
                this.dataTableConfig["oLanguage"] = {
                    "sSearch": "Filter:"
                };
                if (this.dataTable) {
                    this.dataTable.fnClearTable(false);
                    this.dataTable.fnAddData(array);
                    this.dataTable.fnDraw();
                }
                else {
                    this.dataTable = tableElement.dataTable(this.dataTableConfig);
                }
                var widget = this;
                if (this.dataTable) {
                    var keys = new KeyTable({
                        "table": tableElement[0],
                        "datatable": this.dataTable
                    });
                    keys.fnSetPosition(0, 0);
                    if (angular.isArray(data) && data.length) {
                        var selected = data[0];
                        var selectHandler = widget.config.selectHandler;
                        if (selected && selectHandler) {
                            selectHandler(selected);
                        }
                    }
                }
                $(tableElement).focus();
                var widget = this;
                var expandCollapseNode = function () {
                    var dataTable = widget.dataTable;
                    var parentRow = this.parentNode;
                    var openMessages = widget.openMessages;
                    var i = $.inArray(parentRow, openMessages);
                    var element = $('i', this);
                    if (i === -1) {
                        element.removeClass('icon-plus');
                        element.addClass('icon-minus');
                        var dataDiv = formatMessageDetails(dataTable, parentRow);
                        var detailsRow = $(dataTable.fnOpen(parentRow, dataDiv, 'details'));
                        detailsRow.css("padding", "0");
                        setTimeout(function () {
                            detailsRow.find(".innerDetails").slideDown(400, function () {
                                $(parentRow).addClass('opened');
                                openMessages.push(parentRow);
                            });
                        }, 20);
                    }
                    else {
                        $(parentRow.nextSibling).find(".innerDetails").slideUp(400, function () {
                            $(parentRow).removeClass('opened');
                            element.removeClass('icon-minus');
                            element.addClass('icon-plus');
                            dataTable.fnClose(parentRow);
                            openMessages.splice(i, 1);
                        });
                    }
                    Core.$apply($scope);
                };
                if (!this.addedExpandNodes) {
                    this.addedExpandNodes = true;
                    $(tableElement).on("click", "td.control", expandCollapseNode);
                    keys.event.action(0, null, function (node) {
                        expandCollapseNode.call(node);
                    });
                }
                keys.event.focus(null, null, function (node) {
                    var dataTable = widget.dataTable;
                    var row = node;
                    if (node) {
                        var nodeName = node.nodeName;
                        if (nodeName) {
                            if (nodeName.toLowerCase() === "td") {
                                row = $(node).parents("tr")[0];
                            }
                            var selected = dataTable.fnGetData(row);
                            var selectHandler = widget.config.selectHandler;
                            if (selected && selectHandler) {
                                selectHandler(selected);
                            }
                        }
                    }
                });
                $(tableElement).find("td.control").on("click", function (event) {
                    var dataTable = widget.dataTable;
                    if ($(this).hasClass('selected')) {
                        $(this).removeClass('focus selected');
                    }
                    else {
                        if (!widget.config.multiSelect) {
                            dataTable.$('td.selected').removeClass('focus selected');
                        }
                        $(this).addClass('focus selected');
                        var row = $(this).parents("tr")[0];
                        var selected = dataTable.fnGetData(row);
                        var selectHandler = widget.config.selectHandler;
                        if (selected && selectHandler) {
                            selectHandler(selected);
                        }
                    }
                });
            }
            Core.$apply($scope);
        };
        TableWidget.prototype.populateDetailDiv = function (row, div) {
            delete row["0"];
            var scope = this.scope.$new();
            scope.row = row;
            scope.templateDiv = div;
            var template = this.detailTemplate;
            if (!template) {
                var templateId = this.config.rowDetailTemplateId;
                if (templateId) {
                    this.detailTemplate = this.$templateCache.get(templateId);
                    template = this.detailTemplate;
                }
            }
            if (template) {
                div.html(template);
                this.$compile(div.contents())(scope);
            }
        };
        return TableWidget;
    })();
    DataTable.TableWidget = TableWidget;
})(DataTable || (DataTable = {}));
var DataTable;
(function (DataTable) {
    DataTable.pluginName = 'datatable';
    DataTable.log = Logger.get("DataTable");
    DataTable._module = angular.module(DataTable.pluginName, ['bootstrap', 'ngResource']);
    DataTable._module.config(["$routeProvider", function ($routeProvider) {
        $routeProvider.when('/datatable/test', { templateUrl: 'app/datatable/html/test.html' });
    }]);
    DataTable._module.directive('hawtioDatatable', ["$templateCache", "$compile", "$timeout", "$filter", function ($templateCache, $compile, $timeout, $filter) {
        return function (scope, element, attrs) {
            var gridOptions = null;
            var data = null;
            var widget = null;
            var timeoutId = null;
            var initialised = false;
            var childScopes = [];
            var rowDetailTemplate = null;
            var rowDetailTemplateId = null;
            var selectedItems = null;
            function updateGrid() {
                Core.$applyNowOrLater(scope);
            }
            function convertToDataTableColumn(columnDef) {
                var data = {
                    mData: columnDef.field,
                    sDefaultContent: ""
                };
                var name = columnDef.displayName;
                if (name) {
                    data["sTitle"] = name;
                }
                var width = columnDef.width;
                if (angular.isNumber(width)) {
                    data["sWidth"] = "" + width + "px";
                }
                else if (angular.isString(width) && !width.startsWith("*")) {
                    data["sWidth"] = width;
                }
                var template = columnDef.cellTemplate;
                if (template) {
                    data["fnCreatedCell"] = function (nTd, sData, oData, iRow, iCol) {
                        var childScope = childScopes[iRow];
                        if (!childScope) {
                            childScope = scope.$new(false);
                            childScopes[iRow] = childScope;
                        }
                        var entity = oData;
                        childScope["row"] = {
                            entity: entity,
                            getProperty: function (name) {
                                return entity[name];
                            }
                        };
                        var elem = $(nTd);
                        elem.html(template);
                        var contents = elem.contents();
                        contents.removeClass("ngCellText");
                        $compile(contents)(childScope);
                    };
                }
                else {
                    var cellFilter = columnDef.cellFilter;
                    var render = columnDef.render;
                    if (cellFilter && !render) {
                        var filter = $filter(cellFilter);
                        if (filter) {
                            render = function (data, type, full) {
                                return filter(data);
                            };
                        }
                    }
                    if (render) {
                        data["mRender"] = render;
                    }
                }
                return data;
            }
            function destroyChildScopes() {
                angular.forEach(childScopes, function (childScope) {
                    childScope.$destroy();
                });
                childScopes = [];
            }
            function selectHandler(selection) {
                if (selection && selectedItems) {
                    selectedItems.splice(0, selectedItems.length);
                    selectedItems.push(selection);
                    Core.$apply(scope);
                }
            }
            function onTableDataChange(value) {
                gridOptions = value;
                if (gridOptions) {
                    selectedItems = gridOptions.selectedItems;
                    rowDetailTemplate = gridOptions.rowDetailTemplate;
                    rowDetailTemplateId = gridOptions.rowDetailTemplateId;
                    if (widget === null) {
                        var widgetOptions = {
                            selectHandler: selectHandler,
                            disableAddColumns: true,
                            rowDetailTemplateId: rowDetailTemplateId,
                            ignoreColumns: gridOptions.ignoreColumns,
                            flattenColumns: gridOptions.flattenColumns
                        };
                        var rootElement = $(element);
                        var tableElement = rootElement.children("table");
                        if (!tableElement.length) {
                            $("<table class='table table-bordered table-condensed'></table>").appendTo(rootElement);
                            tableElement = rootElement.children("table");
                        }
                        tableElement.removeClass('table-striped');
                        tableElement.addClass('dataTable');
                        var trElement = Core.getOrCreateElements(tableElement, ["thead", "tr"]);
                        destroyChildScopes();
                        var columns = [];
                        var columnCounter = 1;
                        var extraLeftColumn = rowDetailTemplate || rowDetailTemplateId;
                        if (extraLeftColumn) {
                            columns.push({
                                "mDataProp": null,
                                "sClass": "control center",
                                "sWidth": "30px",
                                "sDefaultContent": '<i class="icon-plus"></i>'
                            });
                            var th = trElement.children("th");
                            if (th.length < columnCounter++) {
                                $("<th></th>").appendTo(trElement);
                            }
                        }
                        var columnDefs = gridOptions.columnDefs;
                        if (angular.isString(columnDefs)) {
                            columnDefs = scope[columnDefs];
                        }
                        angular.forEach(columnDefs, function (columnDef) {
                            th = trElement.children("th");
                            if (th.length < columnCounter++) {
                                var name = columnDef.displayName || "";
                                $("<th>" + name + "</th>").appendTo(trElement);
                            }
                            columns.push(convertToDataTableColumn(columnDef));
                        });
                        widget = new DataTable.TableWidget(scope, $templateCache, $compile, columns, widgetOptions);
                        widget.tableElement = tableElement;
                        var sortInfo = gridOptions.sortInfo;
                        if (sortInfo && columnDefs) {
                            var sortColumns = [];
                            var field = sortInfo.field;
                            if (field) {
                                var idx = columnDefs.findIndex({ field: field });
                                if (idx >= 0) {
                                    if (extraLeftColumn) {
                                        idx += 1;
                                    }
                                    var asc = sortInfo.direction || "asc";
                                    asc = asc.toLowerCase();
                                    sortColumns.push([idx, asc]);
                                }
                            }
                            if (sortColumns.length) {
                                widget.sortColumns = sortColumns;
                            }
                        }
                        if (columns.every(function (col) { return col.sWidth; })) {
                            widget.dataTableConfig.bAutoWidth = false;
                        }
                        var filterText = null;
                        var filterOptions = gridOptions.filterOptions;
                        if (filterOptions) {
                            filterText = filterOptions.filterText;
                        }
                        if (filterText || (angular.isDefined(gridOptions.showFilter) && !gridOptions.showFilter)) {
                            widget.dataTableConfig.sDom = 'Rlrtip';
                        }
                        if (filterText) {
                            scope.$watch(filterText, function (value) {
                                var dataTable = widget.dataTable;
                                if (dataTable) {
                                    dataTable.fnFilter(value);
                                }
                            });
                        }
                        if (angular.isDefined(gridOptions.displayFooter) && !gridOptions.displayFooter && widget.dataTableConfig.sDom) {
                            widget.dataTableConfig.sDom = widget.dataTableConfig.sDom.replace('i', '');
                        }
                    }
                    if (!data) {
                        data = gridOptions.data;
                        if (data) {
                            var listener = function (value) {
                                if (initialised || (value && (!angular.isArray(value) || value.length))) {
                                    initialised = true;
                                    destroyChildScopes();
                                    widget.populateTable(value);
                                    updateLater();
                                }
                            };
                            scope.$watch(data, listener);
                            scope.$on("hawtio.datatable." + data, function (args) {
                                var value = Core.pathGet(scope, data);
                                listener(value);
                            });
                        }
                    }
                }
                updateGrid();
            }
            scope.$watch(attrs.hawtioDatatable, onTableDataChange);
            function updateLater() {
                timeoutId = $timeout(function () {
                    updateGrid();
                }, 300);
            }
            element.bind('$destroy', function () {
                destroyChildScopes();
                $timeout.cancel(timeoutId);
            });
            updateLater();
        };
    }]);
    hawtioPluginLoader.addModule(DataTable.pluginName);
})(DataTable || (DataTable = {}));
var FilterHelpers;
(function (FilterHelpers) {
    FilterHelpers.log = Logger.get("FilterHelpers");
    function search(object, filter, maxDepth, and) {
        if (maxDepth === void 0) { maxDepth = -1; }
        if (and === void 0) { and = true; }
        var f = filter.split(" ");
        var matches = f.filter(function (f) {
            return searchObject(object, f, maxDepth);
        });
        if (and) {
            return matches.length === f.length;
        }
        else {
            return matches.length > 0;
        }
    }
    FilterHelpers.search = search;
    function searchObject(object, filter, maxDepth, depth) {
        if (maxDepth === void 0) { maxDepth = -1; }
        if (depth === void 0) { depth = 0; }
        if ((maxDepth > 0 && depth >= maxDepth) || depth > 50) {
            return false;
        }
        var f = filter.toLowerCase();
        var answer = false;
        if (angular.isString(object)) {
            answer = object.toLowerCase().has(f);
        }
        else if (angular.isNumber(object)) {
            answer = ("" + object).toLowerCase().has(f);
        }
        else if (angular.isArray(object)) {
            answer = object.some(function (item) {
                return searchObject(item, f, maxDepth, depth + 1);
            });
        }
        else if (angular.isObject(object)) {
            answer = searchObject(Object.extended(object).values(), f, maxDepth, depth);
        }
        return answer;
    }
    FilterHelpers.searchObject = searchObject;
})(FilterHelpers || (FilterHelpers = {}));
var DataTable;
(function (DataTable) {
    var SimpleDataTable = (function () {
        function SimpleDataTable($compile) {
            var _this = this;
            this.$compile = $compile;
            this.restrict = 'A';
            this.scope = {
                config: '=hawtioSimpleTable',
                target: '@',
                showFiles: '@'
            };
            this.link = function ($scope, $element, $attrs) {
                return _this.doLink($scope, $element, $attrs);
            };
        }
        SimpleDataTable.prototype.doLink = function ($scope, $element, $attrs) {
            var defaultPrimaryKeyFn = function (entity, idx) {
                return entity["id"] || entity["_id"] || entity["name"] || idx;
            };
            var config = $scope.config;
            var dataName = config.data || "data";
            var primaryKeyFn = config.primaryKeyFn || defaultPrimaryKeyFn;
            $scope.rows = [];
            var scope = $scope.$parent || $scope;
            var listener = function (otherValue) {
                var value = Core.pathGet(scope, dataName);
                if (value && !angular.isArray(value)) {
                    value = [value];
                    Core.pathSet(scope, dataName, value);
                }
                if (!('sortInfo' in config) && 'columnDefs' in config) {
                    var ds = config.columnDefs.first()['defaultSort'];
                    var sortField;
                    if (angular.isUndefined(ds) || ds === true) {
                        sortField = config.columnDefs.first()['field'];
                    }
                    else {
                        sortField = config.columnDefs.slice(1).first()['field'];
                    }
                    config['sortInfo'] = {
                        sortBy: sortField,
                        ascending: true
                    };
                }
                else {
                    config['sortInfo'] = {
                        sortBy: '',
                        ascending: true
                    };
                }
                var sortInfo = $scope.config.sortInfo;
                var idx = -1;
                $scope.rows = (value || []).sortBy(sortInfo.sortBy, !sortInfo.ascending).map(function (entity) {
                    idx++;
                    return {
                        entity: entity,
                        index: idx,
                        getProperty: function (name) {
                            return entity[name];
                        }
                    };
                });
                Core.pathSet(scope, ['hawtioSimpleTable', dataName, 'rows'], $scope.rows);
                var reSelectedItems = [];
                $scope.rows.forEach(function (row, idx) {
                    var rpk = primaryKeyFn(row.entity, row.index);
                    var selected = config.selectedItems.some(function (s) {
                        var spk = primaryKeyFn(s, s.index);
                        return angular.equals(rpk, spk);
                    });
                    if (selected) {
                        row.entity.index = row.index;
                        reSelectedItems.push(row.entity);
                        DataTable.log.debug("Data changed so keep selecting row at index " + row.index);
                    }
                });
                config.selectedItems = reSelectedItems;
            };
            scope.$watch(dataName, listener);
            scope.$on("hawtio.datatable." + dataName, listener);
            function getSelectionArray() {
                var selectionArray = config.selectedItems;
                if (!selectionArray) {
                    selectionArray = [];
                    config.selectedItems = selectionArray;
                }
                if (angular.isString(selectionArray)) {
                    var name = selectionArray;
                    selectionArray = Core.pathGet(scope, name);
                    if (!selectionArray) {
                        selectionArray = [];
                        scope[name] = selectionArray;
                    }
                }
                return selectionArray;
            }
            function isMultiSelect() {
                var multiSelect = $scope.config.multiSelect;
                if (angular.isUndefined(multiSelect)) {
                    multiSelect = true;
                }
                return multiSelect;
            }
            $scope.toggleAllSelections = function () {
                var allRowsSelected = $scope.config.allRowsSelected;
                var newFlag = allRowsSelected;
                var selectionArray = getSelectionArray();
                selectionArray.splice(0, selectionArray.length);
                angular.forEach($scope.rows, function (row) {
                    row.selected = newFlag;
                    if (allRowsSelected) {
                        selectionArray.push(row.entity);
                    }
                });
            };
            $scope.toggleRowSelection = function (row) {
                if (row) {
                    var selectionArray = getSelectionArray();
                    if (!isMultiSelect()) {
                        selectionArray.splice(0, selectionArray.length);
                        angular.forEach($scope.rows, function (r) {
                            if (r !== row) {
                                r.selected = false;
                            }
                        });
                    }
                    var entity = row.entity;
                    if (entity) {
                        var idx = selectionArray.indexOf(entity);
                        if (row.selected) {
                            if (idx < 0) {
                                selectionArray.push(entity);
                            }
                        }
                        else {
                            $scope.config.allRowsSelected = false;
                            if (idx >= 0) {
                                selectionArray.splice(idx, 1);
                            }
                        }
                    }
                }
            };
            $scope.sortBy = function (field) {
                if ($scope.config.sortInfo.sortBy === field) {
                    $scope.config.sortInfo.ascending = !$scope.config.sortInfo.ascending;
                }
                else {
                    $scope.config.sortInfo.sortBy = field;
                    $scope.config.sortInfo.ascending = true;
                }
                $scope.$emit("hawtio.datatable." + dataName);
            };
            $scope.getClass = function (field) {
                if ('sortInfo' in $scope.config) {
                    if ($scope.config.sortInfo.sortBy === field) {
                        if ($scope.config.sortInfo.ascending) {
                            return 'asc';
                        }
                        else {
                            return 'desc';
                        }
                    }
                }
                return '';
            };
            $scope.showRow = function (row) {
                var filter = Core.pathGet($scope, ['config', 'filterOptions', 'filterText']);
                if (Core.isBlank(filter)) {
                    return true;
                }
                var data = null;
                try {
                    data = row['entity']['title'];
                }
                catch (e) {
                }
                if (!data) {
                    data = row.entity;
                }
                var match = FilterHelpers.search(data, filter);
                return match;
            };
            $scope.isSelected = function (row) {
                return config.selectedItems.some(row.entity);
            };
            $scope.onRowSelected = function (row) {
                var idx = config.selectedItems.indexOf(row.entity);
                if (idx >= 0) {
                    DataTable.log.debug("De-selecting row at index " + row.index);
                    config.selectedItems.splice(idx, 1);
                }
                else {
                    if (!config.multiSelect) {
                        config.selectedItems.length = 0;
                    }
                    DataTable.log.debug("Selecting row at index " + row.index);
                    row.entity.index = row.index;
                    config.selectedItems.push(row.entity);
                }
            };
            var rootElement = $element;
            rootElement.empty();
            var showCheckBox = firstValueDefined(config, ["showSelectionCheckbox", "displaySelectionCheckbox"], true);
            var enableRowClickSelection = firstValueDefined(config, ["enableRowClickSelection"], false);
            var onMouseDown;
            if (enableRowClickSelection) {
                onMouseDown = "ng-mousedown='onRowSelected(row)' ";
            }
            else {
                onMouseDown = "";
            }
            var headHtml = "<thead><tr>";
            var bodyHtml = "<tbody><tr ng-repeat='row in rows track by $index' ng-show='showRow(row)' " + onMouseDown + "ng-class=\"{'selected': isSelected(row)}\" >";
            var idx = 0;
            if (showCheckBox) {
                var toggleAllHtml = isMultiSelect() ? "<input type='checkbox' ng-show='rows.length' ng-model='config.allRowsSelected' ng-change='toggleAllSelections()'>" : "";
                headHtml += "\n<th class='simple-table-checkbox'>" + toggleAllHtml + "</th>";
                bodyHtml += "\n<td class='simple-table-checkbox'><input type='checkbox' ng-model='row.selected' ng-change='toggleRowSelection(row)'></td>";
            }
            angular.forEach(config.columnDefs, function (colDef) {
                var field = colDef.field;
                var cellTemplate = colDef.cellTemplate || '<div class="ngCellText" title="{{row.entity.' + field + '}}">{{row.entity.' + field + '}}</div>';
                headHtml += "\n<th class='clickable no-fade table-header' ng-click=\"sortBy('" + field + "')\" ng-class=\"getClass('" + field + "')\">{{config.columnDefs[" + idx + "].displayName}}<span class='indicator'></span></th>";
                bodyHtml += "\n<td>" + cellTemplate + "</td>";
                idx += 1;
            });
            var html = headHtml + "\n</tr></thead>\n" + bodyHtml + "\n</tr></tbody>";
            var newContent = this.$compile(html)($scope);
            rootElement.html(newContent);
        };
        return SimpleDataTable;
    })();
    DataTable.SimpleDataTable = SimpleDataTable;
    function firstValueDefined(object, names, defaultValue) {
        var answer = defaultValue;
        var found = false;
        angular.forEach(names, function (name) {
            var value = object[name];
            if (!found && angular.isDefined(value)) {
                answer = value;
                found = true;
            }
        });
        return answer;
    }
    DataTable._module.directive('hawtioSimpleTable', ["$compile", function ($compile) { return new DataTable.SimpleDataTable($compile); }]);
})(DataTable || (DataTable = {}));
var ES;
(function (ES) {
    ES.config = {
        elasticsearch: "http://" + window.location.hostname + ":9200",
        indice: "twitter",
        doctype: "tweet",
        query: "*"
    };
})(ES || (ES = {}));
var ES;
(function (ES) {
    function isEmptyObject(value) {
        return $.isEmptyObject(value);
    }
    ES.isEmptyObject = isEmptyObject;
    function SearchCtrl($scope, $location, $log, ejsResource) {
        var esServer = $scope.esServer = ES.config["elasticsearch"];
        var query = $scope.queryTerm = ES.config["query"];
        var facetField = $scope.facetField = "tags";
        var facetType = $scope.facetType = "terms";
        var index = $scope.indice = ES.config["indice"];
        var type = $scope.docType = ES.config["doctype"];
        var ejs;
        var request;
        $scope.log = $log;
        $scope.search = function () {
            if (isEmptyObject(ejs)) {
                console.log("Init EJS server");
                ejs = initElasticsearchServer(esServer);
            }
            setupEsRequest();
            request = request.query(ejs.QueryStringQuery(query));
            var results = request.doSearch();
            console.log("Do Elastic Search");
            results.then(function (results) {
                $scope.queryTerm = "";
                if (typeof results.error != 'undefined') {
                    console.error("ES error : " + results.error);
                    return;
                }
                console.log(results.hits.total + " : results retrieved");
                $scope.results = results;
            });
        };
        $scope.facetTermsSearch = function () {
            if (isEmptyObject(ejs)) {
                console.log("Init EJS server");
                ejs = initElasticsearchServer(esServer);
            }
            setupEsRequest();
            if (!isEmptyObject($scope.facetField)) {
                facetField = $scope.facetField;
            }
            if (!isEmptyObject($scope.facetType)) {
                facetType = $scope.facetType;
            }
            request = request.query(ejs.QueryStringQuery(query)).facet(ejs.TermsFacet("termFacet").field(facetField).size(50));
            var results = request.doSearch();
            console.log("Do Elastic Search");
            results.then(function (results) {
                $scope.queryTerm = "";
                if (typeof results.error != 'undefined') {
                    console.error("ES error : " + results.error);
                    return;
                }
                console.log(results.hits.total + " : results retrieved");
                $scope.results = results;
            });
        };
        $scope.facetDateHistogramSearch = function () {
            if (isEmptyObject(ejs)) {
                console.log("Init EJS server");
                ejs = initElasticsearchServer(esServer);
            }
            setupEsRequest();
            if (!isEmptyObject($scope.facetField)) {
                facetField = $scope.facetField;
            }
            if (!isEmptyObject($scope.facetType)) {
                facetType = $scope.facetType;
            }
            request = request.query(ejs.QueryStringQuery(query)).facet(ejs.DateHistogramFacet("dateHistoFacet").field(facetField).interval("minute"));
            var results = request.doSearch();
            console.log("Do Elastic Search");
            results.then(function (results) {
                $scope.queryTerm = "";
                if (typeof results.error != 'undefined') {
                    console.error("ES error : " + results.error);
                    return;
                }
                console.log(results.hits.total + " : results retrieved");
                $scope.results = results;
            });
        };
        $scope.indexSampleDocs = function () {
            var host = "http://" + location.host;
            if (isEmptyObject(ejs)) {
                console.log("EJS object is not defined - create it - setupEsRequest");
                ejs = initElasticsearchServer(esServer);
            }
            var docs = [];
            $.getJSON(host + "/hawtio/app/elasticsearch/js/data.json", function (result) {
                $.each(result, function (i, field) {
                    console.log("Field : " + field);
                    docs[i] = ejs.Document(index, type, i).source(field);
                    docs[i].refresh(true).doIndex();
                });
            });
        };
        function setupEsRequest() {
            console.log("ES Server = " + $scope.esServer);
            console.log("Indice = " + $scope.indice);
            console.log("Type = " + $scope.docType);
            console.log("Query = " + $scope.queryTerm);
            if (!isEmptyObject($scope.indice)) {
                index = $scope.indice;
            }
            if (!isEmptyObject($scope.esServer)) {
                esServer = $scope.esServer;
            }
            if (!isEmptyObject($scope.docType)) {
                type = $scope.docType;
            }
            if (!isEmptyObject($scope.queryTerm)) {
                query = $scope.queryTerm;
            }
            var ejs = ejsResource($scope.esServer);
            request = ejs.Request().indices(index).types(type);
            console.log("Request to call ElasticSearch defined");
        }
        function initElasticsearchServer(esServer) {
            return ejsResource(esServer);
        }
        $scope.parse_error = function (data) {
            var _error = data.match("nested: (.*?);");
            return _error == null ? data : _error[1];
        };
    }
    ES.SearchCtrl = SearchCtrl;
})(ES || (ES = {}));
var ES;
(function (ES) {
    var pluginName = 'elasticsearch';
    var base_url = 'app/elasticsearch/html';
    ES._module = angular.module(pluginName, ['bootstrap', 'ngResource', 'elasticjs.service', 'dangle']);
    ES._module.config(['$routeProvider', function ($routeProvider) {
        $routeProvider.when('/elasticsearch', { templateUrl: base_url + '/es.html' });
    }]);
    ES._module.run(["$location", "workspace", "viewRegistry", "helpRegistry", function ($location, workspace, viewRegistry, helpRegistry) {
        viewRegistry[pluginName] = 'app/elasticsearch/html/es.html';
        helpRegistry.addUserDoc(pluginName, 'app/elasticsearch/doc/help.md', function () {
            return false;
        });
    }]);
})(ES || (ES = {}));
var ObjectHelpers;
(function (ObjectHelpers) {
    function toMap(arr, index, decorator) {
        if (!arr || arr.length === 0) {
            return {};
        }
        var answer = {};
        arr.forEach(function (item) {
            if (angular.isObject(item)) {
                answer[item[index]] = item;
                if (angular.isFunction(decorator)) {
                    decorator(item);
                }
            }
        });
        return answer;
    }
    ObjectHelpers.toMap = toMap;
})(ObjectHelpers || (ObjectHelpers = {}));
var SelectionHelpers;
(function (SelectionHelpers) {
    var log = Logger.get("SelectionHelpers");
    function selectNone(group) {
        group.forEach(function (item) {
            item['selected'] = false;
        });
    }
    SelectionHelpers.selectNone = selectNone;
    function selectAll(group, filter) {
        group.forEach(function (item) {
            if (!filter) {
                item['selected'] = true;
            }
            else {
                if (filter(item)) {
                    item['selected'] = true;
                }
            }
        });
    }
    SelectionHelpers.selectAll = selectAll;
    function toggleSelection(item) {
        item['selected'] = !item['selected'];
    }
    SelectionHelpers.toggleSelection = toggleSelection;
    function selectOne(group, item) {
        selectNone(group);
        toggleSelection(item);
    }
    SelectionHelpers.selectOne = selectOne;
    function sync(selections, group, index) {
        group.forEach(function (item) {
            item['selected'] = selections.any(function (selection) {
                return selection[index] === item[index];
            });
        });
        return group.filter(function (item) {
            return item['selected'];
        });
    }
    SelectionHelpers.sync = sync;
    function select(group, item, $event) {
        var ctrlKey = $event.ctrlKey;
        if (!ctrlKey) {
            if (item['selected']) {
                toggleSelection(item);
            }
            else {
                selectOne(group, item);
            }
        }
        else {
            toggleSelection(item);
        }
    }
    SelectionHelpers.select = select;
    function isSelected(item, yes, no) {
        return maybe(item['selected'], yes, no);
    }
    SelectionHelpers.isSelected = isSelected;
    function clearGroup(group) {
        group.length = 0;
    }
    SelectionHelpers.clearGroup = clearGroup;
    function toggleSelectionFromGroup(group, item, search) {
        var searchMethod = search || item;
        if (group.any(searchMethod)) {
            group.remove(searchMethod);
        }
        else {
            group.add(item);
        }
    }
    SelectionHelpers.toggleSelectionFromGroup = toggleSelectionFromGroup;
    function stringOrBoolean(str, answer) {
        if (angular.isDefined(str)) {
            return str;
        }
        else {
            return answer;
        }
    }
    function nope(str) {
        return stringOrBoolean(str, false);
    }
    function yup(str) {
        return stringOrBoolean(str, true);
    }
    function maybe(answer, yes, no) {
        if (answer) {
            return yup(yes);
        }
        else {
            return nope(no);
        }
    }
    function isInGroup(group, item, yes, no, search) {
        if (!group) {
            return nope(no);
        }
        var searchMethod = search || item;
        return maybe(group.any(searchMethod), yes, no);
    }
    SelectionHelpers.isInGroup = isInGroup;
    function filterByGroup(group, item, yes, no, search) {
        if (group.length === 0) {
            return yup(yes);
        }
        var searchMethod = search || item;
        if (angular.isArray(item)) {
            return maybe(group.intersect(item).length === group.length, yes, no);
        }
        else {
            return maybe(group.any(searchMethod), yes, no);
        }
    }
    SelectionHelpers.filterByGroup = filterByGroup;
    function syncGroupSelection(group, collection, attribute) {
        var newGroup = [];
        if (attribute) {
            group.forEach(function (groupItem) {
                var first = collection.find(function (collectionItem) {
                    return groupItem[attribute] === collectionItem[attribute];
                });
                if (first) {
                    newGroup.push(first);
                }
            });
        }
        else {
            group.forEach(function (groupItem) {
                var first = collection.find(function (collectionItem) {
                    return Object.equal(groupItem, collectionItem);
                });
                if (first) {
                    newGroup.push(first);
                }
            });
        }
        clearGroup(group);
        group.add(newGroup);
    }
    SelectionHelpers.syncGroupSelection = syncGroupSelection;
    function decorate($scope) {
        $scope.selectNone = selectNone;
        $scope.selectAll = selectAll;
        $scope.toggleSelection = toggleSelection;
        $scope.selectOne = selectOne;
        $scope.select = select;
        $scope.clearGroup = clearGroup;
        $scope.toggleSelectionFromGroup = toggleSelectionFromGroup;
        $scope.isInGroup = isInGroup;
        $scope.viewOnly = false;
        $scope.filterByGroup = filterByGroup;
    }
    SelectionHelpers.decorate = decorate;
})(SelectionHelpers || (SelectionHelpers = {}));
var ProfileHelpers;
(function (ProfileHelpers) {
    function getTags(profile) {
        var answer = profile.tags;
        if (!answer || !answer.length) {
            answer = profile.id.split('-');
            answer = answer.first(answer.length - 1);
        }
        return answer;
    }
    ProfileHelpers.getTags = getTags;
})(ProfileHelpers || (ProfileHelpers = {}));
var Forms;
(function (Forms) {
    Forms.log = Logger.get("Forms");
    function defaultValues(entity, schema) {
        if (entity && schema) {
            angular.forEach(schema.properties, function (property, key) {
                var defaultValue = property.default;
                if (defaultValue && !entity[key]) {
                    console.log("===== defaulting value " + defaultValue + " into entity[" + key + "]");
                    entity[key] = defaultValue;
                }
            });
        }
    }
    Forms.defaultValues = defaultValues;
    function resolveTypeNameAlias(type, schema) {
        if (type && schema) {
            var alias = lookupDefinition(type, schema);
            if (alias) {
                var realType = alias["type"];
                if (realType) {
                    type = realType;
                }
            }
        }
        return type;
    }
    Forms.resolveTypeNameAlias = resolveTypeNameAlias;
    function isJsonType(name, schema, typeName) {
        var definition = lookupDefinition(name, schema);
        while (definition) {
            var extendsTypes = Core.pathGet(definition, ["extends", "type"]);
            if (extendsTypes) {
                if (typeName === extendsTypes) {
                    return true;
                }
                else {
                    definition = lookupDefinition(extendsTypes, schema);
                }
            }
            else {
                return false;
            }
        }
        return false;
    }
    Forms.isJsonType = isJsonType;
    function safeIdentifier(id) {
        if (id) {
            return id.replace(/-/g, "_");
        }
        return id;
    }
    Forms.safeIdentifier = safeIdentifier;
    function lookupDefinition(name, schema) {
        if (schema) {
            var defs = schema.definitions;
            if (defs) {
                var answer = defs[name];
                if (answer) {
                    var fullSchema = answer["fullSchema"];
                    if (fullSchema) {
                        return fullSchema;
                    }
                    var extendsTypes = Core.pathGet(answer, ["extends", "type"]);
                    if (extendsTypes) {
                        fullSchema = angular.copy(answer);
                        fullSchema.properties = fullSchema.properties || {};
                        if (!angular.isArray(extendsTypes)) {
                            extendsTypes = [extendsTypes];
                        }
                        angular.forEach(extendsTypes, function (extendType) {
                            if (angular.isString(extendType)) {
                                var extendDef = lookupDefinition(extendType, schema);
                                var properties = Core.pathGet(extendDef, ["properties"]);
                                if (properties) {
                                    angular.forEach(properties, function (property, key) {
                                        fullSchema.properties[key] = property;
                                    });
                                }
                            }
                        });
                        answer["fullSchema"] = fullSchema;
                        return fullSchema;
                    }
                }
                return answer;
            }
        }
        return null;
    }
    Forms.lookupDefinition = lookupDefinition;
    function findArrayItemsSchema(property, schema) {
        var items = null;
        if (property && schema) {
            items = property.items;
            if (items) {
                var typeName = items["type"];
                if (typeName) {
                    var definition = lookupDefinition(typeName, schema);
                    if (definition) {
                        return definition;
                    }
                }
            }
            var additionalProperties = property.additionalProperties;
            if (additionalProperties) {
                if (additionalProperties["$ref"] === "#") {
                    return schema;
                }
            }
        }
        return items;
    }
    Forms.findArrayItemsSchema = findArrayItemsSchema;
    function isObjectType(definition) {
        var typeName = Core.pathGet(definition, "type");
        return typeName && "object" === typeName;
    }
    Forms.isObjectType = isObjectType;
    function isArrayOrNestedObject(property, schema) {
        if (property) {
            var propType = resolveTypeNameAlias(property["type"], schema);
            if (propType) {
                if (propType === "object" || propType === "array") {
                    return true;
                }
            }
        }
        return false;
    }
    Forms.isArrayOrNestedObject = isArrayOrNestedObject;
    function configure(config, scopeConfig, attrs) {
        if (angular.isDefined(scopeConfig)) {
            config = angular.extend(config, scopeConfig);
        }
        return angular.extend(config, attrs);
    }
    Forms.configure = configure;
    function getControlGroup(config, arg, id) {
        var rc = angular.element('<div class="' + config.controlgroupclass + '"></div>');
        if (angular.isDefined(arg.description)) {
            rc.attr('title', arg.description);
        }
        if (config['properties'] && config['properties'][id]) {
            var elementConfig = config['properties'][id];
            if (elementConfig && 'control-attributes' in elementConfig) {
                angular.forEach(elementConfig['control-attributes'], function (value, key) {
                    rc.attr(key, value);
                });
            }
        }
        return rc;
    }
    Forms.getControlGroup = getControlGroup;
    function getLabel(config, arg, label) {
        return angular.element('<label class="' + config.labelclass + '">' + label + ': </label>');
    }
    Forms.getLabel = getLabel;
    function getControlDiv(config) {
        return angular.element('<div class="' + config.controlclass + '"></div>');
    }
    Forms.getControlDiv = getControlDiv;
    function getHelpSpan(config, arg, id) {
        var help = Core.pathGet(config.data, ['properties', id, 'help']);
        if (!Core.isBlank(help)) {
            return angular.element('<span class="help-block">' + help + '</span>');
        }
        else {
            return angular.element('<span class="help-block"></span>');
        }
    }
    Forms.getHelpSpan = getHelpSpan;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    function createWidget(propTypeName, property, schema, config, id, ignorePrefixInLabel, configScopeName, wrapInGroup, disableHumanizeLabel) {
        if (wrapInGroup === void 0) { wrapInGroup = true; }
        if (disableHumanizeLabel === void 0) { disableHumanizeLabel = false; }
        var input = null;
        var group = null;
        function copyElementAttributes(element, propertyName) {
            var propertyAttributes = property[propertyName];
            if (propertyAttributes) {
                angular.forEach(propertyAttributes, function (value, key) {
                    if (angular.isString(value)) {
                        element.attr(key, value);
                    }
                });
            }
        }
        function copyAttributes() {
            copyElementAttributes(input, "input-attributes");
            angular.forEach(property, function (value, key) {
                if (angular.isString(value) && key.indexOf("$") < 0 && key !== "type") {
                    var html = Core.escapeHtml(value);
                    input.attr(key, html);
                }
            });
        }
        var options = {
            valueConverter: null
        };
        var safeId = Forms.safeIdentifier(id);
        var inputMarkup = createStandardWidgetMarkup(propTypeName, property, schema, config, options, safeId);
        if (inputMarkup) {
            input = angular.element(inputMarkup);
            copyAttributes();
            id = safeId;
            var modelName = config.model || Core.pathGet(property, ["input-attributes", "ng-model"]);
            if (!modelName) {
                modelName = config.getEntity() + "." + id;
            }
            input.attr("ng-model", modelName);
            input.attr('name', id);
            try {
                if (config.isReadOnly()) {
                    input.attr('readonly', 'true');
                }
            }
            catch (e) {
            }
            var title = property.tooltip || property.label;
            if (title) {
                input.attr('title', title);
            }
            var disableHumanizeLabelValue = disableHumanizeLabel || property.disableHumanizeLabel;
            var defaultLabel = id;
            if (ignorePrefixInLabel || property.ignorePrefixInLabel) {
                var idx = id.lastIndexOf('.');
                if (idx > 0) {
                    defaultLabel = id.substring(idx + 1);
                }
            }
            if (input.attr("type") !== "hidden" && wrapInGroup) {
                group = this.getControlGroup(config, config, id);
                var labelText = property.title || property.label || (disableHumanizeLabelValue ? defaultLabel : Core.humanizeValue(defaultLabel));
                var labelElement = Forms.getLabel(config, config, labelText);
                if (title) {
                    labelElement.attr('title', title);
                }
                group.append(labelElement);
                copyElementAttributes(labelElement, "label-attributes");
                var controlDiv = Forms.getControlDiv(config);
                controlDiv.append(input);
                controlDiv.append(Forms.getHelpSpan(config, config, id));
                group.append(controlDiv);
                copyElementAttributes(controlDiv, "control-attributes");
                copyElementAttributes(group, "control-group-attributes");
                var scope = config.scope;
                if (scope && modelName) {
                    var onModelChange = function (newValue) {
                        scope.$emit("hawtio.form.modelChange", modelName, newValue);
                    };
                    var fn = onModelChange;
                    var converterFn = options.valueConverter;
                    if (converterFn) {
                        fn = function () {
                            converterFn(scope, modelName);
                            var newValue = Core.pathGet(scope, modelName);
                            onModelChange(newValue);
                        };
                    }
                    scope.$watch(modelName, fn);
                }
            }
        }
        else {
            input = angular.element('<div></div>');
            input.attr(Forms.normalize(propTypeName, property, schema), '');
            copyAttributes();
            input.attr('entity', config.getEntity());
            input.attr('mode', config.getMode());
            var fullSchemaName = config.schemaName;
            if (fullSchemaName) {
                input.attr('schema', fullSchemaName);
            }
            if (configScopeName) {
                input.attr('data', configScopeName);
            }
            if (ignorePrefixInLabel || property.ignorePrefixInLabel) {
                input.attr('ignore-prefix-in-label', true);
            }
            if (disableHumanizeLabel || property.disableHumanizeLabel) {
                input.attr('disable-humanize-label', true);
            }
            input.attr('name', id);
        }
        var label = property.label;
        if (label) {
            input.attr('title', label);
        }
        if (property.required) {
            if (input[0].localName === "input" && input.attr("type") === "checkbox") {
            }
            else {
                input.attr('required', 'true');
            }
        }
        return group ? group : input;
    }
    Forms.createWidget = createWidget;
    function createStandardWidgetMarkup(propTypeName, property, schema, config, options, id) {
        var type = Forms.resolveTypeNameAlias(propTypeName, schema);
        if (!type) {
            return '<input type="text"/>';
        }
        var custom = Core.pathGet(property, ["formTemplate"]);
        if (custom) {
            return null;
        }
        var inputElement = Core.pathGet(property, ["input-element"]);
        if (inputElement) {
            return "<" + inputElement + "></" + inputElement + ">";
        }
        var enumValues = Core.pathGet(property, ["enum"]);
        if (enumValues) {
            var required = true;
            var valuesScopeName = null;
            var attributes = "";
            if (enumValues) {
                var scope = config.scope;
                var data = config.data;
                if (data && scope) {
                    var fullSchema = scope[config.schemaName];
                    var model = angular.isString(data) ? scope[data] : data;
                    var paths = id.split(".");
                    var property = null;
                    angular.forEach(paths, function (path) {
                        property = Core.pathGet(model, ["properties", path]);
                        var typeName = Core.pathGet(property, ["type"]);
                        var alias = Forms.lookupDefinition(typeName, fullSchema);
                        if (alias) {
                            model = alias;
                        }
                    });
                    var values = Core.pathGet(property, ["enum"]);
                    valuesScopeName = "$values_" + id.replace(/\./g, "_");
                    scope[valuesScopeName] = values;
                }
            }
            if (valuesScopeName) {
                attributes += ' ng-options="value for value in ' + valuesScopeName + '"';
            }
            var defaultOption = required ? "" : '<option value=""></option>';
            return '<select' + attributes + '>' + defaultOption + '</select>';
        }
        if (angular.isArray(type)) {
            return null;
        }
        if (!angular.isString(type)) {
            return null;
        }
        var defaultValueConverter = null;
        var defaultValue = property.default;
        if (defaultValue) {
            defaultValueConverter = function (scope, modelName) {
                var value = Core.pathGet(scope, modelName);
                if (!value) {
                    Core.pathSet(scope, modelName, property.default);
                }
            };
            options.valueConverter = defaultValueConverter;
        }
        function getModelValueOrDefault(scope, modelName) {
            var value = Core.pathGet(scope, modelName);
            if (!value) {
                var defaultValue = property.default;
                if (defaultValue) {
                    value = defaultValue;
                    Core.pathSet(scope, modelName, value);
                }
            }
            return value;
        }
        switch (type.toLowerCase()) {
            case "int":
            case "integer":
            case "long":
            case "short":
            case "java.lang.integer":
            case "java.lang.long":
            case "float":
            case "double":
            case "java.lang.float":
            case "java.lang.double":
                options.valueConverter = function (scope, modelName) {
                    var value = getModelValueOrDefault(scope, modelName);
                    if (value && angular.isString(value)) {
                        var numberValue = Number(value);
                        Core.pathSet(scope, modelName, numberValue);
                    }
                };
                return '<input type="number"/>';
            case "array":
            case "java.lang.array":
            case "java.lang.iterable":
            case "java.util.list":
            case "java.util.collection":
            case "java.util.iterator":
            case "java.util.set":
            case "object[]":
                return null;
            case "boolean":
            case "bool":
            case "java.lang.boolean":
                options.valueConverter = function (scope, modelName) {
                    var value = getModelValueOrDefault(scope, modelName);
                    if (value && "true" === value) {
                        Core.pathSet(scope, modelName, true);
                    }
                };
                return '<input type="checkbox"/>';
            case "password":
                return '<input type="password"/>';
            case "hidden":
                return '<input type="hidden"/>';
            case "map":
                return null;
            default:
                return '<input type="text"/>';
        }
    }
    Forms.createStandardWidgetMarkup = createStandardWidgetMarkup;
    function mapType(type) {
        switch (type.toLowerCase()) {
            case "int":
            case "integer":
            case "long":
            case "short":
            case "java.lang.integer":
            case "java.lang.long":
            case "float":
            case "double":
            case "java.lang.float":
            case "java.lang.double":
                return "number";
            case "array":
            case "java.lang.array":
            case "java.lang.iterable":
            case "java.util.list":
            case "java.util.collection":
            case "java.util.iterator":
            case "java.util.set":
            case "object[]":
                return "text";
            case "boolean":
            case "bool":
            case "java.lang.boolean":
                return "checkbox";
            case "password":
                return "password";
            case "hidden":
                return "hidden";
            default:
                return "text";
        }
    }
    Forms.mapType = mapType;
    function normalize(type, property, schema) {
        type = Forms.resolveTypeNameAlias(type, schema);
        if (!type) {
            return "hawtio-form-text";
        }
        var custom = Core.pathGet(property, ["formTemplate"]);
        if (custom) {
            return "hawtio-form-custom";
        }
        var enumValues = Core.pathGet(property, ["enum"]);
        if (enumValues) {
            return "hawtio-form-select";
        }
        if (angular.isArray(type)) {
            return null;
        }
        if (!angular.isString(type)) {
            try {
                console.log("Unsupported JSON schema type value " + JSON.stringify(type));
            }
            catch (e) {
                console.log("Unsupported JSON schema type value " + type);
            }
            return null;
        }
        switch (type.toLowerCase()) {
            case "int":
            case "integer":
            case "long":
            case "short":
            case "java.lang.integer":
            case "java.lang.long":
            case "float":
            case "double":
            case "java.lang.float":
            case "java.lang.double":
                return "hawtio-form-number";
            case "array":
            case "java.lang.array":
            case "java.lang.iterable":
            case "java.util.list":
            case "java.util.collection":
            case "java.util.iterator":
            case "java.util.set":
            case "object[]":
                var items = property.items;
                if (items) {
                    var typeName = items.type;
                    if (typeName && typeName === "string") {
                        return "hawtio-form-string-array";
                    }
                }
                else {
                    return "hawtio-form-string-array";
                }
                Forms.log.debug("Returning hawtio-form-array for : ", property);
                return "hawtio-form-array";
            case "boolean":
            case "bool":
            case "java.lang.boolean":
                return "hawtio-form-checkbox";
            case "password":
                return "hawtio-form-password";
            case "hidden":
                return "hawtio-form-hidden";
            case "map":
                return "hawtio-form-map";
            default:
                return "hawtio-form-text";
        }
    }
    Forms.normalize = normalize;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    var SimpleFormConfig = (function () {
        function SimpleFormConfig() {
            this.name = 'form';
            this.method = 'post';
            this.entity = 'entity';
            this.schemaName = 'schema';
            this.mode = 'edit';
            this.data = {};
            this.json = undefined;
            this.scope = null;
            this.scopeName = null;
            this.properties = [];
            this.action = '';
            this.formclass = 'hawtio-form form-horizontal no-bottom-margin';
            this.controlgroupclass = 'control-group';
            this.controlclass = 'controls';
            this.labelclass = 'control-label';
            this.showtypes = 'false';
            this.onsubmit = 'onSubmit';
        }
        SimpleFormConfig.prototype.getMode = function () {
            return this.mode || "edit";
        };
        SimpleFormConfig.prototype.getEntity = function () {
            return this.entity || "entity";
        };
        SimpleFormConfig.prototype.isReadOnly = function () {
            return this.getMode() === "view";
        };
        return SimpleFormConfig;
    })();
    Forms.SimpleFormConfig = SimpleFormConfig;
    var SimpleForm = (function () {
        function SimpleForm(workspace, $compile) {
            var _this = this;
            this.workspace = workspace;
            this.$compile = $compile;
            this.restrict = 'A';
            this.scope = true;
            this.replace = true;
            this.transclude = true;
            this.attributeName = 'simpleForm';
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        SimpleForm.prototype.isReadOnly = function () {
            return false;
        };
        SimpleForm.prototype.doLink = function (scope, element, attrs) {
            var config = new SimpleFormConfig;
            var fullSchemaName = attrs["schema"];
            var fullSchema = fullSchemaName ? scope[fullSchemaName] : null;
            var compiledNode = null;
            var childScope = null;
            var tabs = null;
            var fieldset = null;
            var schema = null;
            var configScopeName = attrs[this.attributeName] || attrs["data"];
            var firstControl = null;
            var simple = this;
            scope.$watch(configScopeName, onWidgetDataChange);
            function onWidgetDataChange(scopeData) {
                if (scopeData) {
                    onScopeData(scopeData);
                }
            }
            function onScopeData(scopeData) {
                config = Forms.configure(config, scopeData, attrs);
                config.schemaName = fullSchemaName;
                config.scopeName = configScopeName;
                config.scope = scope;
                var entityName = config.getEntity();
                if (angular.isDefined(config.json)) {
                    config.data = $.parseJSON(config.json);
                }
                else {
                    config.data = scopeData;
                }
                var form = simple.createForm(config);
                fieldset = form.find('fieldset');
                schema = config.data;
                tabs = {
                    elements: {},
                    locations: {},
                    use: false
                };
                if (schema && angular.isDefined(schema.tabs)) {
                    tabs.use = true;
                    tabs['div'] = $('<div class="tabbable hawtio-form-tabs"></div>');
                    angular.forEach(schema.tabs, function (value, key) {
                        tabs.elements[key] = $('<div class="tab-pane" title="' + key + '"></div>');
                        tabs['div'].append(tabs.elements[key]);
                        value.forEach(function (val) {
                            tabs.locations[val] = key;
                        });
                    });
                    if (!tabs.locations['*']) {
                        tabs.locations['*'] = Object.extended(schema.tabs).keys()[0];
                    }
                }
                if (!tabs.use) {
                    fieldset.append('<div class="spacer"></div>');
                }
                if (schema) {
                    if (tabs.use) {
                        var tabKeyToIdPropObject = {};
                        angular.forEach(schema.properties, function (property, id) {
                            var tabkey = findTabOrderValue(id);
                            var array = tabKeyToIdPropObject[tabkey];
                            if (!array) {
                                array = [];
                                tabKeyToIdPropObject[tabkey] = array;
                            }
                            array.push({ id: id, property: property });
                        });
                        angular.forEach(schema.tabs, function (value, key) {
                            value.forEach(function (val) {
                                var array = tabKeyToIdPropObject[val];
                                if (array) {
                                    angular.forEach(array, function (obj) {
                                        var id = obj.id;
                                        var property = obj.property;
                                        if (id && property) {
                                            addProperty(id, property);
                                        }
                                    });
                                }
                            });
                        });
                    }
                    else {
                        angular.forEach(schema.properties, function (property, id) {
                            addProperty(id, property);
                        });
                    }
                }
                if (tabs.use) {
                    var tabDiv = tabs['div'];
                    var tabCount = Object.keys(tabs.elements).length;
                    if (tabCount < 2) {
                        angular.forEach(tabDiv.children().children(), function (control) {
                            fieldset.append(control);
                        });
                    }
                    else {
                        fieldset.append(tabDiv);
                    }
                }
                var findFunction = function (scope, func) {
                    if (angular.isDefined(scope[func]) && angular.isFunction(scope[func])) {
                        return scope;
                    }
                    if (angular.isDefined(scope.$parent) && scope.$parent !== null) {
                        return findFunction(scope.$parent, func);
                    }
                    else {
                        return null;
                    }
                };
                var onSubmitFunc = config.onsubmit.replace('(', '').replace(')', '');
                var onSubmit = maybeGet(findFunction(scope, onSubmitFunc), onSubmitFunc);
                if (onSubmit === null) {
                    onSubmit = function (json, form) {
                        Forms.log.info("No submit handler defined for form:", form.get(0).name);
                    };
                }
                if (angular.isDefined(onSubmit)) {
                    form.submit(function () {
                        Forms.log.debug("child scope: ", childScope);
                        Forms.log.debug("form name: ", config);
                        if (childScope[config.name].$invalid) {
                            return false;
                        }
                        var entity = scope[entityName];
                        onSubmit(entity, form);
                        return false;
                    });
                }
                fieldset.append('<input type="submit" style="position: absolute; left: -9999px; width: 1px; height: 1px;">');
                var autoFocus = form.find("*[autofocus]");
                if (!autoFocus || !autoFocus.length) {
                    if (firstControl) {
                        console.log("No autofocus element, so lets add one!");
                        var input = firstControl.find("input").first() || firstControl.find("select").first();
                        if (input) {
                            input.attr("autofocus", "true");
                        }
                    }
                }
                if (compiledNode) {
                    $(compiledNode).remove();
                }
                if (childScope) {
                    childScope.$destroy();
                }
                childScope = scope.$new(false);
                compiledNode = simple.$compile(form)(childScope);
                var formsScopeProperty = "forms";
                var forms = scope[formsScopeProperty];
                if (!forms) {
                    forms = {};
                    scope[formsScopeProperty] = forms;
                }
                var formName = config.name;
                if (formName) {
                    var formObject = childScope[formName];
                    if (formObject) {
                        forms[formName] = formObject;
                    }
                    var formScope = formName += "$scope";
                    forms[formScope] = childScope;
                }
                $(element).append(compiledNode);
            }
            function findTabKey(id) {
                var tabkey = tabs.locations[id];
                if (!tabkey) {
                    angular.forEach(tabs.locations, function (value, key) {
                        if (!tabkey && key !== "*" && id.match(key)) {
                            tabkey = value;
                        }
                    });
                }
                if (!tabkey) {
                    tabkey = tabs.locations['*'];
                }
                return tabkey;
            }
            function findTabOrderValue(id) {
                var answer = null;
                angular.forEach(schema.tabs, function (value, key) {
                    value.forEach(function (val) {
                        if (!answer && val !== "*" && id.match(val)) {
                            answer = val;
                        }
                    });
                });
                if (!answer) {
                    answer = '*';
                }
                return answer;
            }
            function addProperty(id, property, ignorePrefixInLabel) {
                if (ignorePrefixInLabel === void 0) { ignorePrefixInLabel = property.ignorePrefixInLabel; }
                var propTypeName = property.type;
                if ("java.lang.String" === propTypeName) {
                    propTypeName = "string";
                }
                var propSchema = Forms.lookupDefinition(propTypeName, schema);
                if (!propSchema) {
                    propSchema = Forms.lookupDefinition(propTypeName, fullSchema);
                }
                var disableHumanizeLabel = schema ? schema.disableHumanizeLabel : false;
                if (property.hidden) {
                    return;
                }
                var nestedProperties = null;
                if (!propSchema && "object" === propTypeName && property.properties) {
                    nestedProperties = property.properties;
                }
                else if (propSchema && Forms.isObjectType(propSchema)) {
                    nestedProperties = propSchema.properties;
                }
                if (nestedProperties) {
                    angular.forEach(nestedProperties, function (childProp, childId) {
                        var newId = id + "." + childId;
                        addProperty(newId, childProp, property.ignorePrefixInLabel);
                    });
                }
                else {
                    var wrapInGroup = true;
                    var input = Forms.createWidget(propTypeName, property, schema, config, id, ignorePrefixInLabel, configScopeName, wrapInGroup, disableHumanizeLabel);
                    if (tabs.use) {
                        var tabkey = findTabKey(id);
                        tabs.elements[tabkey].append(input);
                    }
                    else {
                        fieldset.append(input);
                    }
                    if (!firstControl) {
                        firstControl = input;
                    }
                }
            }
            function maybeGet(scope, func) {
                if (scope !== null) {
                    return scope[func];
                }
                return null;
            }
        };
        SimpleForm.prototype.createForm = function (config) {
            var form = $('<form class="' + config.formclass + '" novalidate><fieldset></fieldset></form>');
            form.attr('name', config.name);
            form.attr('action', config.action);
            form.attr('method', config.method);
            form.find('fieldset').append(this.getLegend(config));
            return form;
        };
        SimpleForm.prototype.getLegend = function (config) {
            var description = Core.pathGet(config, "data.description");
            if (description) {
                return '<legend>' + description + '</legend>';
            }
            return '';
        };
        return SimpleForm;
    })();
    Forms.SimpleForm = SimpleForm;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    var InputTableConfig = (function () {
        function InputTableConfig() {
            this.name = 'form';
            this.method = 'post';
            this.entity = 'entity';
            this.tableConfig = 'tableConfig';
            this.mode = 'edit';
            this.data = {};
            this.json = undefined;
            this.properties = [];
            this.action = '';
            this.tableclass = 'table table-striped inputTable';
            this.controlgroupclass = 'control-group';
            this.controlclass = 'controls pull-right';
            this.labelclass = 'control-label';
            this.showtypes = 'true';
            this.removeicon = 'icon-remove';
            this.editicon = 'icon-edit';
            this.addicon = 'icon-plus';
            this.removetext = 'Remove';
            this.edittext = 'Edit';
            this.addtext = 'Add';
            this.onadd = 'onadd';
            this.onedit = 'onedit';
            this.onremove = 'onRemove';
            this.primaryKeyProperty = undefined;
        }
        InputTableConfig.prototype.getTableConfig = function () {
            return this.tableConfig || "tableConfig";
        };
        return InputTableConfig;
    })();
    Forms.InputTableConfig = InputTableConfig;
    var InputTable = (function () {
        function InputTable(workspace, $compile) {
            var _this = this;
            this.workspace = workspace;
            this.$compile = $compile;
            this.restrict = 'A';
            this.scope = true;
            this.replace = true;
            this.transclude = true;
            this.attributeName = 'hawtioInputTable';
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        InputTable.prototype.doLink = function (scope, element, attrs) {
            var _this = this;
            var config = new InputTableConfig;
            var configName = attrs[this.attributeName];
            var tableConfig = Core.pathGet(scope, configName);
            config = Forms.configure(config, tableConfig, attrs);
            var entityName = attrs["entity"] || config.data || "entity";
            var propertyName = attrs["property"] || "arrayData";
            var entityPath = entityName + "." + propertyName;
            var primaryKeyProperty = config.primaryKeyProperty;
            var tableName = config["title"] || entityName;
            if (angular.isDefined(config.json)) {
                config.data = $.parseJSON(config.json);
            }
            else {
                config.data = scope[config.data];
            }
            var div = $("<div></div>");
            var tableConfig = Core.pathGet(scope, configName);
            if (!tableConfig) {
                console.log("No table configuration for table " + tableName);
            }
            else {
                tableConfig["selectedItems"] = [];
                scope.config = tableConfig;
            }
            var table = this.createTable(config, configName);
            var group = this.getControlGroup(config, {}, "");
            var controlDiv = this.getControlDiv(config);
            controlDiv.addClass('btn-group');
            group.append(controlDiv);
            function updateData(action) {
                var data = Core.pathGet(scope, entityPath);
                if (!data) {
                    data = [];
                }
                if (!angular.isArray(data) && data) {
                    data = [data];
                }
                data = action(data);
                Core.pathSet(scope, entityPath, data);
                scope.$emit("hawtio.datatable." + entityPath, data);
                Core.$apply(scope);
            }
            function removeSelected(data) {
                angular.forEach(scope.config.selectedItems, function (selected) {
                    var id = selected["_id"];
                    if (angular.isArray(data)) {
                        data = data.remove(function (value) { return Object.equal(value, selected); });
                        delete selected["_id"];
                        data = data.remove(function (value) { return Object.equal(value, selected); });
                    }
                    else {
                        delete selected["_id"];
                        if (id) {
                            delete data[id];
                        }
                        else {
                            var found = false;
                            angular.forEach(data, function (value, key) {
                                if (!found && (Object.equal(value, selected))) {
                                    console.log("Found row to delete! " + key);
                                    delete data[key];
                                    found = true;
                                }
                            });
                            if (!found) {
                                console.log("Could not find " + JSON.stringify(selected) + " in " + JSON.stringify(data));
                            }
                        }
                    }
                });
                return data;
            }
            var add = null;
            var edit = null;
            var remove = null;
            var addDialog = null;
            var editDialog = null;
            var readOnly = attrs["readonly"];
            if (!readOnly) {
                var property = null;
                var dataName = attrs["data"];
                var dataModel = dataName ? Core.pathGet(scope, dataName) : null;
                var schemaName = attrs["schema"] || dataName;
                var schema = schemaName ? Core.pathGet(scope, schemaName) : null;
                if (propertyName && dataModel) {
                    property = Core.pathGet(dataModel, ["properties", propertyName]);
                }
                add = this.getAddButton(config);
                scope.addDialogOptions = {
                    backdropFade: true,
                    dialogFade: true
                };
                scope.showAddDialog = false;
                scope.openAddDialog = function () {
                    scope.addEntity = {};
                    scope.addFormConfig = Forms.findArrayItemsSchema(property, schema);
                    var childDataModelName = "addFormConfig";
                    if (!addDialog) {
                        var title = "Add " + tableName;
                        addDialog = $('<div modal="showAddDialog" close="closeAddDialog()" options="addDialogOptions">\n' + '<div class="modal-header"><h4>' + title + '</h4></div>\n' + '<div class="modal-body"><div simple-form="addFormConfig" entity="addEntity" data="' + childDataModelName + '" schema="' + schemaName + '"></div></div>\n' + '<div class="modal-footer">' + '<button class="btn btn-primary add" type="button" ng-click="addAndCloseDialog()">Add</button>' + '<button class="btn btn-warning cancel" type="button" ng-click="closeAddDialog()">Cancel</button>' + '</div></div>');
                        div.append(addDialog);
                        _this.$compile(addDialog)(scope);
                    }
                    scope.showAddDialog = true;
                    Core.$apply(scope);
                };
                scope.closeAddDialog = function () {
                    scope.showAddDialog = false;
                    scope.addEntity = {};
                };
                scope.addAndCloseDialog = function () {
                    var newData = scope.addEntity;
                    Forms.log.info("About to add the new entity " + JSON.stringify(newData));
                    if (newData) {
                        updateData(function (data) {
                            if (primaryKeyProperty) {
                                data.remove(function (entity) { return entity[primaryKeyProperty] === newData[primaryKeyProperty]; });
                            }
                            data.push(newData);
                            return data;
                        });
                    }
                    scope.closeAddDialog();
                };
                edit = this.getEditButton(config);
                scope.editDialogOptions = {
                    backdropFade: true,
                    dialogFade: true
                };
                scope.showEditDialog = false;
                scope.openEditDialog = function () {
                    var selected = scope.config.selectedItems;
                    var editObject = {};
                    if (selected && selected.length) {
                        angular.copy(selected[0], editObject);
                    }
                    scope.editEntity = editObject;
                    scope.editFormConfig = Forms.findArrayItemsSchema(property, schema);
                    if (!editDialog) {
                        var title = "Edit " + tableName;
                        editDialog = $('<div modal="showEditDialog" close="closeEditDialog()" options="editDialogOptions">\n' + '<div class="modal-header"><h4>' + title + '</h4></div>\n' + '<div class="modal-body"><div simple-form="editFormConfig" entity="editEntity"></div></div>\n' + '<div class="modal-footer">' + '<button class="btn btn-primary save" type="button" ng-click="editAndCloseDialog()">Save</button>' + '<button class="btn btn-warning cancel" type="button" ng-click="closeEditDialog()">Cancel</button>' + '</div></div>');
                        div.append(editDialog);
                        _this.$compile(editDialog)(scope);
                    }
                    scope.showEditDialog = true;
                    Core.$apply(scope);
                };
                scope.closeEditDialog = function () {
                    scope.showEditDialog = false;
                    scope.editEntity = {};
                };
                scope.editAndCloseDialog = function () {
                    var newData = scope.editEntity;
                    console.log("About to edit the new entity " + JSON.stringify(newData));
                    if (newData) {
                        updateData(function (data) {
                            data = removeSelected(data);
                            data.push(newData);
                            return data;
                        });
                    }
                    scope.closeEditDialog();
                };
                remove = this.getRemoveButton(config);
            }
            var findFunction = function (scope, func) {
                if (angular.isDefined(scope[func]) && angular.isFunction(scope[func])) {
                    return scope;
                }
                if (angular.isDefined(scope.$parent) && scope.$parent !== null) {
                    return findFunction(scope.$parent, func);
                }
                else {
                    return null;
                }
            };
            function maybeGet(scope, func) {
                if (scope !== null) {
                    return scope[func];
                }
                return null;
            }
            var onRemoveFunc = config.onremove.replace('(', '').replace(')', '');
            var onEditFunc = config.onedit.replace('(', '').replace(')', '');
            var onAddFunc = config.onadd.replace('(', '').replace(')', '');
            var onRemove = maybeGet(findFunction(scope, onRemoveFunc), onRemoveFunc);
            var onEdit = maybeGet(findFunction(scope, onEditFunc), onEditFunc);
            var onAdd = maybeGet(findFunction(scope, onAddFunc), onAddFunc);
            if (onRemove === null) {
                onRemove = function () {
                    updateData(function (data) {
                        return removeSelected(data);
                    });
                };
            }
            if (onEdit === null) {
                onEdit = function () {
                    scope.openEditDialog();
                };
            }
            if (onAdd === null) {
                onAdd = function (form) {
                    scope.openAddDialog();
                };
            }
            if (add) {
                add.click(function (event) {
                    onAdd();
                    return false;
                });
                controlDiv.append(add);
            }
            if (edit) {
                edit.click(function (event) {
                    onEdit();
                    return false;
                });
                controlDiv.append(edit);
            }
            if (remove) {
                remove.click(function (event) {
                    onRemove();
                    return false;
                });
                controlDiv.append(remove);
            }
            $(div).append(group);
            $(div).append(table);
            $(element).append(div);
            this.$compile(div)(scope);
        };
        InputTable.prototype.getAddButton = function (config) {
            return $('<button type="button" class="btn add"><i class="' + config.addicon + '"></i> ' + config.addtext + '</button>');
        };
        InputTable.prototype.getEditButton = function (config) {
            return $('<button type="button" class="btn edit" ng-disabled="!config.selectedItems.length"><i class="' + config.editicon + '"></i> ' + config.edittext + '</button>');
        };
        InputTable.prototype.getRemoveButton = function (config) {
            return $('<button type="remove" class="btn remove" ng-disabled="!config.selectedItems.length"><i class="' + config.removeicon + '"></i> ' + config.removetext + '</button>');
        };
        InputTable.prototype.createTable = function (config, tableConfig) {
            var tableType = "hawtio-simple-table";
            var table = $('<table class="' + config.tableclass + '" ' + tableType + '="' + tableConfig + '"></table>');
            return table;
        };
        InputTable.prototype.getLegend = function (config) {
            var description = Core.pathGet(config, "data.description");
            if (description) {
                return '<legend>' + config.data.description + '</legend>';
            }
            return '';
        };
        InputTable.prototype.getControlGroup = function (config, arg, id) {
            var rc = $('<div class="' + config.controlgroupclass + '"></div>');
            if (angular.isDefined(arg.description)) {
                rc.attr('title', arg.description);
            }
            return rc;
        };
        InputTable.prototype.getControlDiv = function (config) {
            return $('<div class="' + config.controlclass + '"></div>');
        };
        InputTable.prototype.getHelpSpan = function (config, arg, id) {
            var rc = $('<span class="help-block"></span>');
            if (angular.isDefined(arg.type) && config.showtypes !== 'false') {
                rc.append('Type: ' + arg.type);
            }
            return rc;
        };
        return InputTable;
    })();
    Forms.InputTable = InputTable;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    var InputBaseConfig = (function () {
        function InputBaseConfig() {
            this.name = 'input';
            this.type = '';
            this.description = '';
            this._default = '';
            this.scope = null;
            this.mode = 'edit';
            this.schemaName = "schema";
            this.controlgroupclass = 'control-group';
            this.controlclass = 'controls';
            this.labelclass = 'control-label';
            this.showtypes = 'false';
            this.formtemplate = null;
            this.entity = 'entity';
            this.model = undefined;
        }
        InputBaseConfig.prototype.getEntity = function () {
            return this.entity || "entity";
        };
        InputBaseConfig.prototype.getMode = function () {
            return this.mode || "edit";
        };
        InputBaseConfig.prototype.isReadOnly = function () {
            return this.getMode() === "view";
        };
        return InputBaseConfig;
    })();
    Forms.InputBaseConfig = InputBaseConfig;
    var InputBase = (function () {
        function InputBase(workspace, $compile) {
            var _this = this;
            this.workspace = workspace;
            this.$compile = $compile;
            this.restrict = 'A';
            this.scope = true;
            this.replace = false;
            this.transclude = false;
            this.attributeName = '';
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        InputBase.prototype.doLink = function (scope, element, attrs) {
            var config = new InputBaseConfig;
            config = Forms.configure(config, null, attrs);
            config.scope = scope;
            config.schemaName = attrs["schema"] || "schema";
            var id = Forms.safeIdentifier(config.name);
            var group = this.getControlGroup(config, config, id);
            var modelName = config.model;
            if (!angular.isDefined(modelName)) {
                modelName = config.getEntity() + "." + id;
            }
            var defaultLabel = id;
            if ("true" === attrs["ignorePrefixInLabel"]) {
                var idx = id.lastIndexOf('.');
                if (idx > 0) {
                    defaultLabel = id.substring(idx + 1);
                }
            }
            var disableHumanizeLabel = "true" === attrs["disableHumanizeLabel"];
            var labelText = attrs["title"] || (disableHumanizeLabel ? defaultLabel : Core.humanizeValue(defaultLabel));
            group.append(Forms.getLabel(config, config, labelText));
            var controlDiv = Forms.getControlDiv(config);
            controlDiv.append(this.getInput(config, config, id, modelName));
            controlDiv.append(Forms.getHelpSpan(config, config, id));
            group.append(controlDiv);
            $(element).append(this.$compile(group)(scope));
            if (scope && modelName) {
                scope.$watch(modelName, onModelChange);
            }
            function onModelChange(newValue) {
                scope.$emit("hawtio.form.modelChange", modelName, newValue);
            }
        };
        InputBase.prototype.getControlGroup = function (config1, config2, id) {
            return Forms.getControlGroup(config1, config2, id);
        };
        InputBase.prototype.getInput = function (config, arg, id, modelName) {
            var rc = $('<span class="form-data"></span>');
            if (modelName) {
                rc.attr('ng-model', modelName);
                rc.append('{{' + modelName + '}}');
            }
            return rc;
        };
        return InputBase;
    })();
    Forms.InputBase = InputBase;
    var TextInput = (function (_super) {
        __extends(TextInput, _super);
        function TextInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
            this.type = "text";
        }
        TextInput.prototype.getInput = function (config, arg, id, modelName) {
            if (config.isReadOnly()) {
                return _super.prototype.getInput.call(this, config, arg, id, modelName);
            }
            var rc = $('<input type="' + this.type + '">');
            rc.attr('name', id);
            if (modelName) {
                rc.attr('ng-model', modelName);
            }
            if (config.isReadOnly()) {
                rc.attr('readonly', 'true');
            }
            var required = config.$attr["required"];
            if (required && required !== "false") {
                rc.attr('required', 'true');
            }
            return rc;
        };
        return TextInput;
    })(InputBase);
    Forms.TextInput = TextInput;
    var HiddenText = (function (_super) {
        __extends(HiddenText, _super);
        function HiddenText(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
            this.type = "hidden";
        }
        HiddenText.prototype.getControlGroup = function (config1, config2, id) {
            var group = _super.prototype.getControlGroup.call(this, config1, config2, id);
            group.css({ 'display': 'none' });
            return group;
        };
        HiddenText.prototype.getInput = function (config, arg, id, modelName) {
            var rc = _super.prototype.getInput.call(this, config, arg, id, modelName);
            rc.attr('readonly', 'true');
            return rc;
        };
        return HiddenText;
    })(TextInput);
    Forms.HiddenText = HiddenText;
    var PasswordInput = (function (_super) {
        __extends(PasswordInput, _super);
        function PasswordInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
            this.type = "password";
        }
        return PasswordInput;
    })(TextInput);
    Forms.PasswordInput = PasswordInput;
    var CustomInput = (function (_super) {
        __extends(CustomInput, _super);
        function CustomInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
        }
        CustomInput.prototype.getInput = function (config, arg, id, modelName) {
            var template = arg.formtemplate;
            template = Core.unescapeHtml(template);
            var rc = $(template);
            if (!rc.attr("name")) {
                rc.attr('name', id);
            }
            if (modelName) {
                rc.attr('ng-model', modelName);
            }
            if (config.isReadOnly()) {
                rc.attr('readonly', 'true');
            }
            return rc;
        };
        return CustomInput;
    })(InputBase);
    Forms.CustomInput = CustomInput;
    var SelectInput = (function (_super) {
        __extends(SelectInput, _super);
        function SelectInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
        }
        SelectInput.prototype.getInput = function (config, arg, id, modelName) {
            if (config.isReadOnly()) {
                return _super.prototype.getInput.call(this, config, arg, id, modelName);
            }
            var required = true;
            var defaultOption = required ? "" : '<option value=""></option>';
            var rc = $('<select>' + defaultOption + '</select>');
            rc.attr('name', id);
            var scope = config.scope;
            var data = config.data;
            if (data && scope) {
                var fullSchema = scope[config.schemaName];
                var model = scope[data];
                var paths = id.split(".");
                var property = null;
                angular.forEach(paths, function (path) {
                    property = Core.pathGet(model, ["properties", path]);
                    var typeName = Core.pathGet(property, ["type"]);
                    var alias = Forms.lookupDefinition(typeName, fullSchema);
                    if (alias) {
                        model = alias;
                    }
                });
                var values = Core.pathGet(property, ["enum"]);
                scope["$selectValues"] = values;
                rc.attr("ng-options", "value for value in $selectValues");
            }
            if (modelName) {
                rc.attr('ng-model', modelName);
            }
            if (config.isReadOnly()) {
                rc.attr('readonly', 'true');
            }
            return rc;
        };
        return SelectInput;
    })(InputBase);
    Forms.SelectInput = SelectInput;
    var NumberInput = (function (_super) {
        __extends(NumberInput, _super);
        function NumberInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
        }
        NumberInput.prototype.getInput = function (config, arg, id, modelName) {
            if (config.isReadOnly()) {
                return _super.prototype.getInput.call(this, config, arg, id, modelName);
            }
            var rc = $('<input type="number">');
            rc.attr('name', id);
            if (angular.isDefined(arg.def)) {
                rc.attr('value', arg.def);
            }
            if (angular.isDefined(arg.minimum)) {
                rc.attr('min', arg.minimum);
            }
            if (angular.isDefined(arg.maximum)) {
                rc.attr('max', arg.maximum);
            }
            if (modelName) {
                rc.attr('ng-model', modelName);
            }
            if (config.isReadOnly()) {
                rc.attr('readonly', 'true');
            }
            var scope = config.scope;
            if (scope) {
                function onModelChange() {
                    var value = Core.pathGet(scope, modelName);
                    if (value && angular.isString(value)) {
                        var numberValue = Number(value);
                        Core.pathSet(scope, modelName, numberValue);
                    }
                }
                scope.$watch(modelName, onModelChange);
                onModelChange();
            }
            return rc;
        };
        return NumberInput;
    })(InputBase);
    Forms.NumberInput = NumberInput;
    var StringArrayInput = (function (_super) {
        __extends(StringArrayInput, _super);
        function StringArrayInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
        }
        StringArrayInput.prototype.getInput = function (config, arg, id, modelName) {
            var rowScopeName = "_" + id;
            var ngRepeat = rowScopeName + ' in ' + modelName;
            var readOnlyWidget = '{{' + rowScopeName + '}}';
            if (config.isReadOnly()) {
                return angular.element('<ul><li ng-repeat="' + rowScopeName + ' in ' + modelName + '">' + readOnlyWidget + '</li></ul>');
            }
            else {
                var scope = config.scope;
                var fallbackSchemaName = (arg.$attr || {})["schema"] || "schema";
                var schema = scope[config.schemaName] || scope[fallbackSchemaName] || {};
                var properties = schema.properties || {};
                var arrayProperty = properties[id] || {};
                var property = arrayProperty["items"] || {};
                var propTypeName = property.type;
                var ignorePrefixInLabel = true;
                var disableHumanizeLabel = property.disableHumanizeLabel;
                var configScopeName = null;
                var value = Core.pathGet(scope, modelName);
                if (!value) {
                    Core.pathSet(scope, modelName, []);
                }
                var methodPrefix = "_form_stringArray" + rowScopeName + "_";
                var itemKeys = methodPrefix + "keys";
                var addMethod = methodPrefix + "add";
                var removeMethod = methodPrefix + "remove";
                function updateKeys() {
                    var value = Core.pathGet(scope, modelName);
                    scope[itemKeys] = value ? Object.keys(value) : [];
                    scope.$emit("hawtio.form.modelChange", modelName, value);
                }
                updateKeys();
                scope[addMethod] = function () {
                    var value = Core.pathGet(scope, modelName) || [];
                    value.push("");
                    Core.pathSet(scope, modelName, value);
                    updateKeys();
                };
                scope[removeMethod] = function (idx) {
                    var value = Core.pathGet(scope, modelName) || [];
                    if (idx < value.length) {
                        value.splice(idx, 1);
                    }
                    Core.pathSet(scope, modelName, value);
                    updateKeys();
                };
                var itemId = modelName + "[" + rowScopeName + "]";
                var itemsConfig = {
                    model: itemId
                };
                var wrapInGroup = false;
                var widget = Forms.createWidget(propTypeName, property, schema, itemsConfig, itemId, ignorePrefixInLabel, configScopeName, wrapInGroup, disableHumanizeLabel);
                if (!widget) {
                    widget = angular.element(readOnlyWidget);
                }
                var markup = angular.element('<div class="controls" style="white-space: nowrap" ng-repeat="' + rowScopeName + ' in ' + itemKeys + '"></div>');
                markup.append(widget);
                markup.append(angular.element('<a ng-click="' + removeMethod + '(' + rowScopeName + ')" title="Remove this value"><i class="red icon-remove"></i></a>'));
                markup.after(angular.element('<a ng-click="' + addMethod + '()" title="Add a new value"><i class="icon-plus"></i></a>'));
                return markup;
            }
        };
        return StringArrayInput;
    })(InputBase);
    Forms.StringArrayInput = StringArrayInput;
    var ArrayInput = (function (_super) {
        __extends(ArrayInput, _super);
        function ArrayInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
        }
        ArrayInput.prototype.doLink = function (scope, element, attrs) {
            var config = new InputBaseConfig;
            config = Forms.configure(config, null, attrs);
            var id = config.name;
            var dataName = attrs["data"] || "";
            var entityName = attrs["entity"] || config.entity;
            var schemaName = attrs["schema"] || config.schemaName;
            function renderRow(cell, type, data) {
                if (data) {
                    var description = data["description"];
                    if (!description) {
                        angular.forEach(data, function (value, key) {
                            if (value && !description) {
                                description = value;
                            }
                        });
                    }
                    return description;
                }
                return null;
            }
            var tableConfigPaths = ["properties", id, "inputTable"];
            var tableConfig = null;
            Core.pathGet(scope, tableConfigPaths);
            if (!tableConfig) {
                var tableConfigScopeName = tableConfigPaths.join(".");
                var disableHumanizeLabel = "true" === attrs["disableHumanizeLabel"];
                var cellDescription = disableHumanizeLabel ? id : Core.humanizeValue(id);
                tableConfig = {
                    formConfig: config,
                    title: cellDescription,
                    data: config.entity + "." + id,
                    displayFooter: false,
                    showFilter: false,
                    columnDefs: [
                        {
                            field: '_id',
                            displayName: cellDescription,
                            render: renderRow
                        }
                    ]
                };
                Core.pathSet(scope, tableConfigPaths, tableConfig);
            }
            var table = $('<div hawtio-input-table="' + tableConfigScopeName + '" data="' + dataName + '" property="' + id + '" entity="' + entityName + '" schema="' + schemaName + '"></div>');
            if (config.isReadOnly()) {
                table.attr("readonly", "true");
            }
            $(element).append(this.$compile(table)(scope));
        };
        return ArrayInput;
    })(InputBase);
    Forms.ArrayInput = ArrayInput;
    var BooleanInput = (function (_super) {
        __extends(BooleanInput, _super);
        function BooleanInput(workspace, $compile) {
            _super.call(this, workspace, $compile);
            this.workspace = workspace;
            this.$compile = $compile;
        }
        BooleanInput.prototype.getInput = function (config, arg, id, modelName) {
            var rc = $('<input class="hawtio-checkbox" type="checkbox">');
            rc.attr('name', id);
            if (config.isReadOnly()) {
                rc.attr('disabled', 'true');
            }
            if (modelName) {
                rc.attr('ng-model', modelName);
            }
            if (config.isReadOnly()) {
                rc.attr('readonly', 'true');
            }
            var scope = config.scope;
            if (scope) {
                function onModelChange() {
                    var value = Core.pathGet(scope, modelName);
                    if (value && "true" === value) {
                        Core.pathSet(scope, modelName, true);
                    }
                }
                scope.$watch(modelName, onModelChange);
                onModelChange();
            }
            return rc;
        };
        return BooleanInput;
    })(InputBase);
    Forms.BooleanInput = BooleanInput;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    var SubmitForm = (function () {
        function SubmitForm() {
            var _this = this;
            this.restrict = 'A';
            this.scope = true;
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        SubmitForm.prototype.doLink = function (scope, element, attrs) {
            var el = $(element);
            var target = 'form[name=' + attrs['hawtioSubmit'] + ']';
            el.click(function () {
                $(target).submit();
                return false;
            });
        };
        return SubmitForm;
    })();
    Forms.SubmitForm = SubmitForm;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    var ResetForm = (function () {
        function ResetForm() {
            var _this = this;
            this.restrict = 'A';
            this.scope = true;
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        ResetForm.prototype.doLink = function (scope, element, attrs) {
            var el = $(element);
            var target = 'form[name=' + attrs['hawtioReset'] + ']';
            el.click(function () {
                var forms = $(target);
                for (var i = 0; i < forms.length; i++) {
                    forms[i].reset();
                }
                return false;
            });
        };
        return ResetForm;
    })();
    Forms.ResetForm = ResetForm;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    Forms.pluginName = 'hawtio-forms';
    Forms.templateUrl = 'app/forms/html/';
    Forms._module = angular.module(Forms.pluginName, ['bootstrap', 'ngResource', 'hawtioCore', 'datatable', 'ui.bootstrap', 'ui.bootstrap.dialog', 'hawtio-ui']);
    Forms._module.config(["$routeProvider", function ($routeProvider) {
        $routeProvider.when('/forms/test', { templateUrl: 'app/forms/html/test.html' }).when('/forms/testTable', { templateUrl: 'app/forms/html/testTable.html' });
    }]);
    Forms._module.directive('simpleForm', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.SimpleForm(workspace, $compile);
    }]);
    Forms._module.directive('hawtioForm', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.SimpleForm(workspace, $compile);
    }]);
    Forms._module.directive('hawtioInputTable', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.InputTable(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormText', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.TextInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormPassword', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.PasswordInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormHidden', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.HiddenText(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormNumber', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.NumberInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormSelect', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.SelectInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormArray', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.ArrayInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormStringArray', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.StringArrayInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormCheckbox', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.BooleanInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioFormCustom', ["workspace", "$compile", function (workspace, $compile) {
        return new Forms.CustomInput(workspace, $compile);
    }]);
    Forms._module.directive('hawtioSubmit', function () {
        return new Forms.SubmitForm();
    });
    Forms._module.directive('hawtioReset', function () {
        return new Forms.ResetForm();
    });
    Forms._module.run(["helpRegistry", function (helpRegistry) {
        helpRegistry.addDevDoc("forms", 'app/forms/doc/developer.md');
    }]);
    hawtioPluginLoader.addModule(Forms.pluginName);
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    function createFormElement() {
        return {
            type: undefined
        };
    }
    Forms.createFormElement = createFormElement;
    function createFormTabs() {
        return {};
    }
    Forms.createFormTabs = createFormTabs;
    function createFormConfiguration() {
        return {
            properties: {}
        };
    }
    Forms.createFormConfiguration = createFormConfiguration;
    function createFormGridConfiguration() {
        return {
            rowSchema: {},
            rows: []
        };
    }
    Forms.createFormGridConfiguration = createFormGridConfiguration;
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    var formGrid = Forms._module.directive("hawtioFormGrid", ['$templateCache', '$interpolate', '$compile', function ($templateCache, $interpolate, $compile) {
        return {
            restrict: 'A',
            replace: true,
            scope: {
                configuration: '=hawtioFormGrid'
            },
            templateUrl: Forms.templateUrl + 'formGrid.html',
            link: function (scope, element, attrs) {
                function createColumns() {
                    return [];
                }
                function createColumnSequence() {
                    var columns = createColumns();
                    if (angular.isDefined(scope.configuration.rowSchema.columnOrder)) {
                        var order = scope.configuration.rowSchema.columnOrder;
                        order.forEach(function (column) {
                            var property = Core.pathGet(scope.configuration.rowSchema.properties, [column]);
                            Core.pathSet(property, ['key'], column);
                            columns.push(property);
                        });
                    }
                    angular.forEach(scope.configuration.rowSchema.properties, function (property, key) {
                        if (!columns.some(function (c) {
                            return c.key === key;
                        })) {
                            property.key = key;
                            columns.push(property);
                        }
                    });
                    return columns;
                }
                function newHeaderRow() {
                    var header = element.find('thead');
                    header.empty();
                    return header.append($templateCache.get('rowTemplate.html')).find('tr');
                }
                function buildTableHeader(columns) {
                    var headerRow = newHeaderRow();
                    columns.forEach(function (property) {
                        var headingName = property.label || property.key;
                        if (!scope.configuration.rowSchema.disableHumanizeLabel) {
                            headingName = headingName.titleize();
                        }
                        var headerTemplate = property.headerTemplate || $templateCache.get('headerCellTemplate.html');
                        var interpolateFunc = $interpolate(headerTemplate);
                        headerRow.append(interpolateFunc({ label: headingName }));
                    });
                    headerRow.append($templateCache.get("emptyHeaderCellTemplate.html"));
                }
                function clearBody() {
                    var body = element.find('tbody');
                    body.empty();
                    return body;
                }
                function newBodyRow() {
                    return angular.element($templateCache.get('rowTemplate.html'));
                }
                function buildTableBody(columns, parent) {
                    var rows = scope.configuration.rows;
                    rows.forEach(function (row, index) {
                        var tr = newBodyRow();
                        columns.forEach(function (property) {
                            var type = Forms.mapType(property.type);
                            if (type === "number" && "input-attributes" in property) {
                                var template = property.template || $templateCache.get('cellNumberTemplate.html');
                                var interpolateFunc = $interpolate(template);
                                tr.append(interpolateFunc({
                                    row: 'configuration.rows[' + index + ']',
                                    type: type,
                                    key: property.key,
                                    min: (property["input-attributes"].min ? property["input-attributes"].min : ""),
                                    max: (property["input-attributes"].max ? property["input-attributes"].max : "")
                                }));
                            }
                            else {
                                var template = property.template || $templateCache.get('cellTemplate.html');
                                var interpolateFunc = $interpolate(template);
                                tr.append(interpolateFunc({
                                    row: 'configuration.rows[' + index + ']',
                                    type: type,
                                    key: property.key
                                }));
                            }
                        });
                        var func = $interpolate($templateCache.get("deleteRowTemplate.html"));
                        tr.append(func({
                            index: index
                        }));
                        parent.append(tr);
                    });
                }
                scope.removeThing = function (index) {
                    scope.configuration.rows.removeAt(index);
                };
                scope.addThing = function () {
                    scope.configuration.rows.push(scope.configuration.onAdd());
                };
                scope.getHeading = function () {
                    if (Core.isBlank(scope.configuration.rowName)) {
                        return 'items'.titleize();
                    }
                    return scope.configuration.rowName.pluralize().titleize();
                };
                scope.$watch('configuration.noDataTemplate', function (newValue, oldValue) {
                    var noDataTemplate = scope.configuration.noDataTemplate || $templateCache.get('heroUnitTemplate.html');
                    element.find('.nodata').html($compile(noDataTemplate)(scope));
                });
                scope.$watch('configuration.rowSchema', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        var columns = createColumnSequence();
                        buildTableHeader(columns);
                    }
                }, true);
                scope.$watchCollection('configuration.rows', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        var body = clearBody();
                        var columns = createColumnSequence();
                        var tmp = angular.element('<div></div>');
                        buildTableBody(columns, tmp);
                        body.append($compile(tmp.children())(scope));
                    }
                });
            }
        };
    }]);
})(Forms || (Forms = {}));
var Log;
(function (Log) {
    Log.log = Logger.get("Logs");
    function logSourceHref(row) {
        if (!row) {
            return "";
        }
        var log = row.entity;
        if (log) {
            return logSourceHrefEntity(log);
        }
        else {
            return logSourceHrefEntity(row);
        }
    }
    Log.logSourceHref = logSourceHref;
    function treeContainsLogQueryMBean(workspace) {
        return workspace.treeContainsDomainAndProperties('io.fabric8.insight', { type: 'LogQuery' }) || workspace.treeContainsDomainAndProperties('org.fusesource.insight', { type: 'LogQuery' });
    }
    Log.treeContainsLogQueryMBean = treeContainsLogQueryMBean;
    function isSelectionLogQueryMBean(workspace) {
        return workspace.hasDomainAndProperties('io.fabric8.insight', { type: 'LogQuery' }) || workspace.hasDomainAndProperties('org.fusesource.insight', { type: 'LogQuery' });
    }
    Log.isSelectionLogQueryMBean = isSelectionLogQueryMBean;
    function findLogQueryMBean(workspace) {
        var node = workspace.findMBeanWithProperties('io.fabric8.insight', { type: 'LogQuery' });
        if (!node) {
            node = workspace.findMBeanWithProperties('org.fusesource.insight', { type: 'LogQuery' });
        }
        return node ? node.objectName : null;
    }
    Log.findLogQueryMBean = findLogQueryMBean;
    function logSourceHrefEntity(log) {
        var fileName = Log.removeQuestion(log.fileName);
        var className = Log.removeQuestion(log.className);
        var properties = log.properties;
        var mavenCoords = "";
        if (properties) {
            mavenCoords = properties["maven.coordinates"];
        }
        if (mavenCoords && fileName) {
            var link = "#/source/view/" + mavenCoords + "/class/" + className + "/" + fileName;
            var line = log.lineNumber;
            if (line) {
                link += "?line=" + line;
            }
            return link;
        }
        else {
            return "";
        }
    }
    Log.logSourceHrefEntity = logSourceHrefEntity;
    function hasLogSourceHref(log) {
        var properties = log.properties;
        if (!properties) {
            return false;
        }
        var mavenCoords = "";
        if (properties) {
            mavenCoords = properties["maven.coordinates"];
        }
        return angular.isDefined(mavenCoords) && mavenCoords !== "";
    }
    Log.hasLogSourceHref = hasLogSourceHref;
    function hasLogSourceLineHref(log) {
        var line = log["lineNumber"];
        return angular.isDefined(line) && line !== "" && line !== "?";
    }
    Log.hasLogSourceLineHref = hasLogSourceLineHref;
    function removeQuestion(text) {
        return (!text || text === "?") ? null : text;
    }
    Log.removeQuestion = removeQuestion;
    var _stackRegex = /\s*at\s+([\w\.$_]+(\.([\w$_]+))*)\((.*)?:(\d+)\).*\[(.*)\]/;
    function formatStackTrace(exception) {
        if (!exception) {
            return '';
        }
        if (!angular.isArray(exception) && angular.isString(exception)) {
            exception = exception.split('\n');
        }
        if (!angular.isArray(exception)) {
            return "";
        }
        var answer = '<ul class="unstyled">\n';
        exception.each(function (line) {
            answer += "<li>" + Log.formatStackLine(line) + "</li>\n";
        });
        answer += "</ul>\n";
        return answer;
    }
    Log.formatStackTrace = formatStackTrace;
    function formatStackLine(line) {
        var match = _stackRegex.exec(line);
        if (match && match.length > 6) {
            var classAndMethod = match[1];
            var fileName = match[4];
            var line = match[5];
            var mvnCoords = match[6];
            if (classAndMethod && fileName && mvnCoords) {
                var className = classAndMethod;
                var idx = classAndMethod.lastIndexOf('.');
                if (idx > 0) {
                    className = classAndMethod.substring(0, idx);
                }
                var link = "#/source/view/" + mvnCoords + "/class/" + className + "/" + fileName;
                if (angular.isDefined(line)) {
                    link += "?line=" + line;
                }
                return "<div class='stack-line'>  at <a href='" + link + "'>" + classAndMethod + "</a>(<span class='fileName'>" + fileName + "</span>:<span class='lineNumber'>" + line + "</span>)[<span class='mavenCoords'>" + mvnCoords + "</span>]</div>";
            }
        }
        var bold = true;
        if (line) {
            line = line.trim();
            if (line.startsWith('at')) {
                line = '  ' + line;
                bold = false;
            }
        }
        if (bold) {
            return '<pre class="stack-line bold">' + line + '</pre>';
        }
        else {
            return '<pre class="stack-line">' + line + '</pre>';
        }
    }
    Log.formatStackLine = formatStackLine;
    function getLogCacheSize(localStorage) {
        var text = localStorage['logCacheSize'];
        if (text) {
            return parseInt(text);
        }
        return 1000;
    }
    Log.getLogCacheSize = getLogCacheSize;
})(Log || (Log = {}));
var Forms;
(function (Forms) {
    var mapDirective = Forms._module.directive("hawtioFormMap", [function () {
        return {
            restrict: 'A',
            replace: true,
            templateUrl: UrlHelpers.join(Forms.templateUrl, "formMapDirective.html"),
            scope: {
                description: '@',
                entity: '=',
                mode: '=',
                data: '=',
                name: '@'
            },
            link: function (scope, element, attr) {
                scope.deleteKey = function (key) {
                    try {
                        delete scope.entity[scope.name]["" + key];
                    }
                    catch (e) {
                        Forms.log.debug("failed to delete key: ", key, " from entity: ", scope.entity);
                    }
                };
                scope.addItem = function (newItem) {
                    if (!scope.entity) {
                        scope.entity = {};
                    }
                    Core.pathSet(scope.entity, [scope.name, newItem.key], newItem.value);
                    scope.showForm = false;
                };
                scope.$watch('showForm', function (newValue) {
                    if (newValue) {
                        scope.newItem = {
                            key: undefined,
                            value: undefined
                        };
                    }
                });
            }
        };
    }]);
})(Forms || (Forms = {}));
var Forms;
(function (Forms) {
    Forms.FormTestController = Forms._module.controller("Forms.FormTestController", ["$scope", function ($scope) {
        $scope.editing = false;
        $scope.html = "text/html";
        $scope.javascript = "javascript";
        $scope.basicFormEx1Entity = {
            'key': 'Some key',
            'value': 'Some value'
        };
        $scope.basicFormEx1EntityString = angular.toJson($scope.basicFormEx1Entity, true);
        $scope.basicFormEx1Result = '';
        $scope.toggleEdit = function () {
            $scope.editing = !$scope.editing;
        };
        $scope.view = function () {
            if (!$scope.editing) {
                return "view";
            }
            return "edit";
        };
        $scope.basicFormEx1 = '<div simple-form name="some-form" action="#/forms/test" method="post" data="basicFormEx1SchemaObject" entity="basicFormEx1Entity" onSubmit="callThis()"></div>';
        $scope.toObject = function (str) {
            return angular.fromJson(str.replace("'", "\""));
        };
        $scope.fromObject = function (str) {
            return angular.toJson($scope[str], true);
        };
        $scope.basicFormEx1Schema = '' + '{\n' + '   "properties": {\n' + '     "key": {\n' + '       "description": "Argument key",\n' + '       "type": "java.lang.String"\n' + '     },\n' + '     "value": {\n' + '       "description": "Argument Value",\n' + '       "type": "java.lang.String"\n' + '     },\n' + '     "longArg": {\n' + '       "description": "Long argument",\n' + '       "type": "Long",\n' + '       "minimum": "5",\n' + '       "maximum": "10"\n' + '     },\n' + '     "intArg": {\n' + '       "description": "Int argument",\n' + '       "type": "Integer"\n' + '     },\n' + '     "objectArg": {\n' + '       "description": "some object",\n' + '       "type": "object"\n' + '     },\n' + '     "booleanArg": {\n' + '       "description": "Some boolean value",\n' + '       "type": "java.lang.Boolean"\n' + '     }\n' + '   },\n' + '   "description": "Show some stuff in a form",\n' + '   "type": "java.lang.String",\n' + '   "tabs": {\n' + '     "Tab One": ["key", "value"],\n' + '     "Tab Two": ["*"],\n' + '     "Tab Three": ["booleanArg"]\n' + '   }\n' + '}';
        $scope.basicFormEx1SchemaObject = $scope.toObject($scope.basicFormEx1Schema);
        $scope.updateSchema = function () {
            $scope.basicFormEx1SchemaObject = $scope.toObject($scope.basicFormEx1Schema);
        };
        $scope.updateEntity = function () {
            $scope.basicFormEx1Entity = angular.fromJson($scope.basicFormEx1EntityString);
        };
        $scope.hawtioResetEx = '<a class="btn" href="" hawtio-reset="some-form"><i class="icon-refresh"></i> Clear</a>';
        $scope.hawtioSubmitEx = '      <a class="btn" href="" hawtio-submit="some-form"><i class="icon-save"></i> Save</a>';
        $scope.callThis = function (json, form) {
            $scope.basicFormEx1Result = angular.toJson(json, true);
            Core.notification('success', 'Form "' + form.get(0).name + '" submitted...');
            Core.$apply($scope);
        };
        $scope.config = {
            name: 'form-with-config-object',
            action: "/some/url",
            method: "post",
            data: 'setVMOption',
            showtypes: 'false'
        };
        $scope.cheese = {
            key: "keyABC",
            value: "valueDEF",
            intArg: 999
        };
        $scope.onCancel = function (form) {
            Core.notification('success', 'Cancel clicked on form "' + form.get(0).name + '"');
        };
        $scope.onSubmit = function (json, form) {
            Core.notification('success', 'Form "' + form.get(0).name + '" submitted... (well not really), data:' + JSON.stringify(json));
        };
        $scope.derp = function (json, form) {
            Core.notification('error', 'derp with json ' + JSON.stringify(json));
        };
        $scope.inputTableData = {
            rows: [
                { id: "object1", name: 'foo' },
                { id: "object2", name: 'bar' }
            ]
        };
        $scope.inputTableConfig = {
            data: 'inputTableData.rows',
            displayFooter: false,
            showFilter: false,
            showSelectionCheckbox: false,
            enableRowClickSelection: true,
            primaryKeyProperty: 'id',
            properties: {
                'rows': { items: { type: 'string', properties: {
                    'id': {
                        description: 'Object ID',
                        type: 'java.lang.String'
                    },
                    'name': {
                        description: 'Object Name',
                        type: 'java.lang.String'
                    }
                } } }
            },
            columnDefs: [
                {
                    field: 'id',
                    displayName: 'ID'
                },
                {
                    field: 'name',
                    displayName: 'Name'
                }
            ]
        };
    }]);
})(Forms || (Forms = {}));
var ArrayHelpers;
(function (ArrayHelpers) {
    function removeElements(collection, newCollection, index) {
        if (index === void 0) { index = 'id'; }
        var oldLength = collection.length;
        collection.remove(function (item) {
            return !newCollection.any(function (c) {
                return c[index] === item[index];
            });
        });
        return collection.length !== oldLength;
    }
    ArrayHelpers.removeElements = removeElements;
    function sync(collection, newCollection, index) {
        if (index === void 0) { index = 'id'; }
        var answer = removeElements(collection, newCollection, index);
        newCollection.forEach(function (item) {
            var oldItem = collection.find(function (c) {
                return c[index] === item[index];
            });
            if (!oldItem) {
                answer = true;
                collection.push(item);
            }
            else {
                if (item !== oldItem) {
                    angular.copy(item, oldItem);
                    answer = true;
                }
            }
        });
        return answer;
    }
    ArrayHelpers.sync = sync;
})(ArrayHelpers || (ArrayHelpers = {}));
var Tree;
(function (Tree) {
    Tree.pluginName = 'tree';
    Tree.log = Logger.get("Tree");
    function expandAll(el) {
        treeAction(el, true);
    }
    Tree.expandAll = expandAll;
    function contractAll(el) {
        treeAction(el, false);
    }
    Tree.contractAll = contractAll;
    function treeAction(el, expand) {
        $(el).dynatree("getRoot").visit(function (node) {
            node.expand(expand);
        });
    }
    function sanitize(tree) {
        if (!tree) {
            return;
        }
        if (angular.isArray(tree)) {
            tree.forEach(function (folder) {
                Tree.sanitize(folder);
            });
        }
        var title = tree['title'];
        if (title) {
            tree['title'] = title.unescapeHTML(true).escapeHTML();
        }
        if (tree.children) {
            Tree.sanitize(tree.children);
        }
    }
    Tree.sanitize = sanitize;
    Tree._module = angular.module(Tree.pluginName, ['bootstrap', 'ngResource', 'hawtioCore']);
    Tree._module.directive('hawtioTree', ["workspace", "$timeout", "$location", function (workspace, $timeout, $location) {
        return function (scope, element, attrs) {
            var tree = null;
            var data = null;
            var widget = null;
            var timeoutId = null;
            var onSelectFn = lookupFunction("onselect");
            var onDragStartFn = lookupFunction("ondragstart");
            var onDragEnterFn = lookupFunction("ondragenter");
            var onDropFn = lookupFunction("ondrop");
            function lookupFunction(attrName) {
                var answer = null;
                var fnName = attrs[attrName];
                if (fnName) {
                    answer = Core.pathGet(scope, fnName);
                    if (!angular.isFunction(answer)) {
                        answer = null;
                    }
                }
                return answer;
            }
            var data = attrs.hawtioTree;
            var queryParam = data;
            scope.$watch(data, onWidgetDataChange);
            scope.$on("hawtio.tree." + data, function (args) {
                var value = Core.pathGet(scope, data);
                onWidgetDataChange(value);
            });
            element.bind('$destroy', function () {
                $timeout.cancel(timeoutId);
            });
            updateLater();
            function updateWidget() {
                Core.$applyNowOrLater(scope);
            }
            function onWidgetDataChange(value) {
                tree = value;
                if (tree) {
                    Tree.sanitize(tree);
                }
                if (tree && !widget) {
                    var treeElement = $(element);
                    var children = Core.asArray(tree);
                    var hideRoot = attrs["hideroot"];
                    if ("true" === hideRoot) {
                        children = tree['children'];
                    }
                    var config = {
                        clickFolderMode: 3,
                        onActivate: function (node) {
                            var data = node.data;
                            if (onSelectFn) {
                                onSelectFn(data, node);
                            }
                            else {
                                workspace.updateSelectionNode(data);
                            }
                            Core.$apply(scope);
                        },
                        onClick: function (node, event) {
                            if (event["metaKey"]) {
                                event.preventDefault();
                                var url = $location.absUrl();
                                if (node && node.data) {
                                    var key = node.data["key"];
                                    if (key) {
                                        var hash = $location.search();
                                        hash[queryParam] = key;
                                        var idx = url.indexOf('?');
                                        if (idx <= 0) {
                                            url += "?";
                                        }
                                        else {
                                            url = url.substring(0, idx + 1);
                                        }
                                        url += $.param(hash);
                                    }
                                }
                                window.open(url, '_blank');
                                window.focus();
                                return false;
                            }
                            return true;
                        },
                        persist: false,
                        debugLevel: 0,
                        children: children,
                        dnd: {
                            onDragStart: onDragStartFn ? onDragStartFn : function (node) {
                                console.log("onDragStart!");
                                return true;
                            },
                            onDragEnter: onDragEnterFn ? onDragEnterFn : function (node, sourceNode) {
                                console.log("onDragEnter!");
                                return true;
                            },
                            onDrop: onDropFn ? onDropFn : function (node, sourceNode, hitMode) {
                                console.log("onDrop!");
                                sourceNode.move(node, hitMode);
                                return true;
                            }
                        }
                    };
                    if (!onDropFn && !onDragEnterFn && !onDragStartFn) {
                        delete config["dnd"];
                    }
                    widget = treeElement.dynatree(config);
                    var activatedNode = false;
                    var activateNodeName = attrs["activatenodes"];
                    if (activateNodeName) {
                        var values = scope[activateNodeName];
                        var tree = treeElement.dynatree("getTree");
                        if (values && tree) {
                            angular.forEach(Core.asArray(values), function (value) {
                                tree.activateKey(value);
                                activatedNode = true;
                            });
                        }
                    }
                    var root = treeElement.dynatree("getRoot");
                    if (root) {
                        var onRootName = attrs["onroot"];
                        if (onRootName) {
                            var fn = scope[onRootName];
                            if (fn) {
                                fn(root);
                            }
                        }
                        if (!activatedNode) {
                            var children = root['getChildren']();
                            if (children && children.length) {
                                var child = children[0];
                                child.expand(true);
                                child.activate(true);
                            }
                        }
                    }
                }
                updateWidget();
            }
            function updateLater() {
                timeoutId = $timeout(function () {
                    updateWidget();
                }, 300);
            }
        };
    }]);
    Tree._module.run(["helpRegistry", function (helpRegistry) {
        helpRegistry.addDevDoc(Tree.pluginName, 'app/tree/doc/developer.md');
    }]);
    hawtioPluginLoader.addModule(Tree.pluginName);
})(Tree || (Tree = {}));
var Log;
(function (Log) {
    var pluginName = 'log';
    var hasMBean = false;
    Log._module = angular.module(pluginName, ['bootstrap', 'ngResource', 'ngGrid', 'datatable', 'hawtioCore']);
    Log._module.config(["$routeProvider", function ($routeProvider) {
        $routeProvider.when('/logs', { templateUrl: 'app/log/html/logs.html', reloadOnSearch: false }).when('/openlogs', { redirectTo: function () {
            if (hasMBean) {
                return '/logs';
            }
            else {
                return '/home';
            }
        }, reloadOnSearch: false });
    }]);
    Log._module.run(["$location", "workspace", "viewRegistry", "layoutFull", "helpRegistry", "preferencesRegistry", function ($location, workspace, viewRegistry, layoutFull, helpRegistry, preferencesRegistry) {
        hasMBean = Log.treeContainsLogQueryMBean(workspace);
        viewRegistry['log'] = layoutFull;
        helpRegistry.addUserDoc('log', 'app/log/doc/help.md', function () {
            return Log.treeContainsLogQueryMBean(workspace);
        });
        preferencesRegistry.addTab("Server Logs", "app/log/html/preferences.html", function () {
            return Log.treeContainsLogQueryMBean(workspace);
        });
        workspace.topLevelTabs.push({
            id: "logs",
            content: "Logs",
            title: "View and search the logs of this container",
            isValid: function (workspace) { return Log.treeContainsLogQueryMBean(workspace); },
            href: function () { return "#/logs"; }
        });
        workspace.subLevelTabs.push({
            content: '<i class="icon-list-alt"></i> Log',
            title: "View the logs in this process",
            isValid: function (workspace) { return Log.isSelectionLogQueryMBean(workspace); },
            href: function () { return "#/logs"; }
        });
    }]);
    Log._module.filter('logDateFilter', ["$filter", function ($filter) {
        var standardDateFilter = $filter('date');
        return function (log) {
            if (!log) {
                return null;
            }
            if (log.timestampMs) {
                return standardDateFilter(log.timestampMs, 'yyyy-MM-dd HH:mm:ss.sss');
            }
            else {
                return standardDateFilter(log.timestamp, 'yyyy-MM-dd HH:mm:ss');
            }
        };
    }]);
    hawtioPluginLoader.addModule(pluginName);
})(Log || (Log = {}));
var Log;
(function (Log) {
    var log = Logger.get("Log");
    Log._module.controller("Log.LogController", ["$scope", "$routeParams", "$location", "localStorage", "workspace", "jolokia", "$window", "$document", "$templateCache", function ($scope, $routeParams, $location, localStorage, workspace, jolokia, $window, $document, $templateCache) {
        $scope.sortAsc = true;
        var value = localStorage["logSortAsc"];
        if (angular.isString(value)) {
            $scope.sortAsc = "true" === value;
        }
        $scope.autoScroll = true;
        value = localStorage["logAutoScroll"];
        if (angular.isString(value)) {
            $scope.autoScroll = "true" === value;
        }
        value = localStorage["logBatchSize"];
        $scope.logBatchSize = angular.isNumber(value) ? value : 20;
        $scope.logs = [];
        $scope.showRowDetails = false;
        $scope.showRaw = {
            expanded: false
        };
        var logQueryMBean = Log.findLogQueryMBean(workspace);
        $scope.init = function () {
            $scope.searchText = $routeParams['s'];
            if (!angular.isDefined($scope.searchText)) {
                $scope.searchText = '';
            }
            $scope.filter = {
                logLevelQuery: $routeParams['l'],
                logLevelExactMatch: Core.parseBooleanValue($routeParams['e']),
                messageOnly: Core.parseBooleanValue($routeParams['o'])
            };
            if (!angular.isDefined($scope.filter.logLevelQuery)) {
                $scope.filter.logLevelQuery = '';
            }
            if (!angular.isDefined($scope.filter.logLevelExactMatch)) {
                $scope.filter.logLevelExactMatch = false;
            }
            if (!angular.isDefined($scope.filter.messageOnly)) {
                $scope.filter.messageOnly = false;
            }
        };
        $scope.$on('$routeUpdate', $scope.init);
        $scope.$watch('searchText', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $location.search('s', newValue);
            }
        });
        $scope.$watch('filter.logLevelQuery', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $location.search('l', newValue);
            }
        });
        $scope.$watch('filter.logLevelExactMatch', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $location.search('e', newValue);
            }
        });
        $scope.$watch('filter.messageOnly', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $location.search('o', newValue);
            }
        });
        $scope.init();
        $scope.toTime = 0;
        $scope.logFilter = {
            afterTimestamp: $scope.toTime,
            count: $scope.logBatchSize
        };
        $scope.logFilterJson = JSON.stringify($scope.logFilter);
        $scope.queryJSON = { type: "EXEC", mbean: logQueryMBean, operation: "jsonQueryLogResults", arguments: [$scope.logFilterJson], ignoreErrors: true };
        $scope.logLevels = ["TRACE", "DEBUG", "INFO", "WARN", "ERROR"];
        $scope.logLevelMap = {};
        $scope.skipFields = ['seq'];
        angular.forEach($scope.logLevels, function (name, idx) {
            $scope.logLevelMap[name] = idx;
            $scope.logLevelMap[name.toLowerCase()] = idx;
        });
        $scope.selectedClass = function ($index) {
            if ($index === $scope.selectedRowIndex) {
                return 'selected';
            }
            return '';
        };
        $scope.$watch('selectedRowIndex', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                if (newValue < 0 || newValue > $scope.logs.length) {
                    $scope.selectedRow = null;
                    $scope.showRowDetails = false;
                    return;
                }
                Log.log.debug("New index: ", newValue);
                $scope.selectedRow = $scope.logs[newValue];
                if (!$scope.showRowDetails) {
                    $scope.showRowDetails = true;
                }
            }
        });
        $scope.hasOSGiProps = function (row) {
            if (!row) {
                return false;
            }
            if (!('properties' in row)) {
                return false;
            }
            var props = row.properties;
            var answer = Object.extended(props).keys().any(function (key) {
                return key.startsWith('bundle');
            });
            return answer;
        };
        $scope.selectRow = function ($index) {
            if ($scope.selectedRowIndex == $index) {
                $scope.showRowDetails = true;
                return;
            }
            $scope.selectedRowIndex = $index;
        };
        $scope.getSelectedRowJson = function () {
            return angular.toJson($scope.selectedRow, true);
        };
        $scope.logClass = function (log) {
            if (!log) {
                return '';
            }
            return logLevelClass(log['level']);
        };
        $scope.logIcon = function (log) {
            if (!log) {
                return '';
            }
            var style = $scope.logClass(log);
            if (style === "error") {
                return "red icon-warning-sign";
            }
            if (style === "warning") {
                return "orange icon-exclamation-sign";
            }
            if (style === "info") {
                return "icon-info-sign";
            }
            return "icon-cog";
        };
        $scope.logSourceHref = Log.logSourceHref;
        $scope.hasLogSourceHref = function (row) {
            if (!row) {
                return false;
            }
            return Log.hasLogSourceHref(row);
        };
        $scope.hasLogSourceLineHref = function (row) {
            if (!row) {
                return false;
            }
            return Log.hasLogSourceLineHref(row);
        };
        $scope.dateFormat = 'yyyy-MM-dd HH:mm:ss';
        $scope.formatException = function (line) {
            return Log.formatStackLine(line);
        };
        $scope.getSupport = function () {
            if (!$scope.selectedRow) {
                return;
            }
            var uri = "https://access.redhat.com/knowledge/solutions";
            var text = $scope.selectedRow.message;
            var logger = $scope.selectedRow.logger;
            uri = uri + "?logger=" + logger + "&text=" + text;
            window.location.href = uri;
        };
        $scope.addToDashboardLink = function () {
            var href = "#/logs";
            var routeParams = angular.toJson($routeParams);
            var size = angular.toJson({
                size_x: 8,
                size_y: 1
            });
            var title = "Logs";
            if ($scope.filter.logLevelQuery !== "") {
                title = title + " LogLevel: " + $scope.filter.logLevelQuery;
            }
            if ($scope.filter.logLevelExactMatch) {
                title = title + " Exact Match";
            }
            if ($scope.searchText !== "") {
                title = title + " Filter: " + $scope.searchText;
            }
            if ($scope.filter.messageOnly) {
                title = title + " Message Only";
            }
            return "#/dashboard/add?tab=dashboard" + "&href=" + encodeURIComponent(href) + "&routeParams=" + encodeURIComponent(routeParams) + "&title=" + encodeURIComponent(title) + "&size=" + encodeURIComponent(size);
        };
        $scope.isInDashboardClass = function () {
            if (angular.isDefined($scope.inDashboard && $scope.inDashboard)) {
                return "log-table-dashboard";
            }
            return "";
        };
        $scope.sortIcon = function () {
            if ($scope.sortAsc) {
                return "icon-arrow-down";
            }
            else {
                return "icon-arrow-up";
            }
        };
        $scope.filterLogMessage = function (log) {
            var messageOnly = $scope.filter.messageOnly;
            if ($scope.filter.logLevelQuery !== "") {
                var logLevelExactMatch = $scope.filter.logLevelExactMatch;
                var logLevelQuery = $scope.filter.logLevelQuery;
                var logLevelQueryOrdinal = (logLevelExactMatch) ? 0 : $scope.logLevelMap[logLevelQuery];
                if (logLevelExactMatch) {
                    if (log.level !== logLevelQuery) {
                        return false;
                    }
                }
                else {
                    var idx = $scope.logLevelMap[log.level];
                    if (!(idx >= logLevelQueryOrdinal || idx < 0)) {
                        return false;
                    }
                }
            }
            if ($scope.searchText.startsWith("l=")) {
                return log.logger.has($scope.searchText.last($scope.searchText.length - 2));
            }
            if ($scope.searchText.startsWith("m=")) {
                return log.message.has($scope.searchText.last($scope.searchText.length - 2));
            }
            if (messageOnly) {
                return log.message.has($scope.searchText);
            }
            return log.logger.has($scope.searchText) || log.message.has($scope.searchText);
        };
        $scope.formatStackTrace = function (exception) {
            if (!exception) {
                return "";
            }
            var answer = '<ul class="unstyled">\n';
            exception.forEach(function (line) {
                answer = answer + '<li>' + $scope.formatException(line) + '</li>';
            });
            answer += '\n</ul>';
            return answer;
        };
        var updateValues = function (response) {
            var scrollToTopOrBottom = false;
            if (!$scope.inDashboard) {
                var window = $($window);
                if ($scope.logs.length === 0) {
                    scrollToTopOrBottom = true;
                }
                if ($scope.sortAsc) {
                    var pos = window.scrollTop() + window.height();
                    var threshold = Core.getDocHeight() - 100;
                }
                else {
                    var pos = window.scrollTop() + window.height();
                    var threshold = 100;
                }
                if (pos > threshold) {
                    scrollToTopOrBottom = true;
                }
            }
            var logs = response.events;
            var toTime = response.toTimestamp;
            if (toTime && angular.isNumber(toTime)) {
                if (toTime < 0) {
                    console.log("ignoring dodgy value of toTime: " + toTime);
                }
                else {
                    $scope.toTime = toTime;
                    $scope.logFilter.afterTimestamp = $scope.toTime;
                    $scope.logFilterJson = JSON.stringify($scope.logFilter);
                    $scope.queryJSON.arguments = [$scope.logFilterJson];
                }
            }
            if (logs) {
                var maxSize = Log.getLogCacheSize(localStorage);
                if ($scope.inDashboard) {
                    maxSize = 10;
                }
                var counter = 0;
                logs.forEach(function (log) {
                    if (log) {
                        if (!$scope.logs.any(function (key, item) { return item.message === log.message && item.seq === log.message && item.timestamp === log.timestamp; })) {
                            counter += 1;
                            if (log.seq != null) {
                                log['timestampMs'] = log.seq;
                            }
                            if ($scope.sortAsc) {
                                $scope.logs.push(log);
                            }
                            else {
                                $scope.logs.unshift(log);
                            }
                        }
                    }
                });
                if (maxSize > 0) {
                    var size = $scope.logs.length;
                    if (size > maxSize) {
                        var count = size - maxSize;
                        var pos = 0;
                        if (!$scope.sortAsc) {
                            pos = size - count;
                        }
                        $scope.logs.splice(pos, count);
                        if ($scope.showRowDetails) {
                            if ($scope.sortAsc) {
                                $scope.selectedRowIndex -= count;
                            }
                            else {
                                $scope.selectedRowIndex += count;
                            }
                        }
                    }
                }
                if (counter) {
                    if ($scope.autoScroll && scrollToTopOrBottom) {
                        setTimeout(function () {
                            var pos = 0;
                            if ($scope.sortAsc) {
                                pos = $document.height() - window.height();
                            }
                            log.debug("Scrolling to position: " + pos);
                            $document.scrollTop(pos);
                        }, 20);
                    }
                    Core.$apply($scope);
                }
            }
        };
        var asyncUpdateValues = function (response) {
            var value = response.value;
            if (value) {
                updateValues(value);
            }
            else {
                Core.notification("error", "Failed to get a response! " + JSON.stringify(response, null, 4));
            }
        };
        var callbackOptions = onSuccess(asyncUpdateValues, {
            error: function (response) {
                asyncUpdateValues(response);
            },
            silent: true
        });
        if (logQueryMBean) {
            var firstCallback = function (results) {
                updateValues(results);
                Core.register(jolokia, $scope, $scope.queryJSON, callbackOptions);
            };
            jolokia.execute(logQueryMBean, "getLogResults(int)", 1000, onSuccess(firstCallback));
        }
    }]);
})(Log || (Log = {}));
var Log;
(function (Log) {
    Log._module.controller("Log.PreferencesController", ["$scope", "localStorage", function ($scope, localStorage) {
        Core.initPreferenceScope($scope, localStorage, {
            'logCacheSize': {
                'value': 1000,
                'converter': parseInt
            },
            'logSortAsc': {
                'value': true,
                'converter': Core.parseBooleanValue
            },
            'logAutoScroll': {
                'value': true,
                'converter': Core.parseBooleanValue
            },
            'logBatchSize': {
                'value': 20,
                'converter': parseInt
            }
        });
    }]);
})(Log || (Log = {}));
var Service;
(function (Service) {
    Service._module = angular.module(Service.pluginName, ['hawtioCore']);
    Service._module.factory("ServiceRegistry", ['$http', '$rootScope', 'workspace', function ($http, $rootScope, workspace) {
        var self = {
            name: 'ServiceRegistry',
            services: [],
            fetch: function (next) {
                if (Kubernetes.isKubernetesTemplateManager(workspace) || Service.pollServices) {
                    $http({
                        method: 'GET',
                        url: 'service'
                    }).success(function (data, status, headers, config) {
                        self.onSuccessfulPoll(next, data, status, headers, config);
                    }).error(function (data, status, headers, config) {
                        self.onFailedPoll(next, data, status, headers, config);
                    });
                }
            },
            onSuccessfulPoll: function (next, data, status, headers, config) {
                var triggerUpdate = ArrayHelpers.sync(self.services, data.items);
                if (triggerUpdate) {
                    Service.log.debug("Services updated: ", self.services);
                    Core.$apply($rootScope);
                }
                next();
            },
            onFailedPoll: function (next, data, status, headers, config) {
                Service.log.debug("Failed poll, data: ", data, " status: ", status);
                next();
            }
        };
        return self;
    }]);
    Service._module.run(['ServiceRegistry', '$timeout', 'jolokia', function (ServiceRegistry, $timeout, jolokia) {
        ServiceRegistry.go = PollHelpers.setupPolling(ServiceRegistry, function (next) {
            ServiceRegistry.fetch(next);
        }, 2000, $timeout, jolokia);
        ServiceRegistry.go();
        Service.log.debug("Loaded");
    }]);
    hawtioPluginLoader.addModule(Service.pluginName);
})(Service || (Service = {}));
var Site;
(function (Site) {
    var pluginName = 'site';
    Site._module = angular.module(pluginName, ['bootstrap', 'ngResource', 'ngGrid', 'datatable', 'hawtioCore', 'hawtio-ui']);
    Site._module.config(["$routeProvider", function ($routeProvider) {
        $routeProvider.when('/site', { templateUrl: 'app/site/html/index.html' }).when('/site/', { templateUrl: 'app/site/html/index.html' }).when('/site/book/*page', { templateUrl: 'app/site/html/book.html', reloadOnSearch: false }).when('/site/*page', { templateUrl: 'app/site/html/page.html' });
    }]);
    Site._module.run(["$location", "workspace", "viewRegistry", "layoutFull", "helpRegistry", function ($location, workspace, viewRegistry, layoutFull, helpRegistry) {
        viewRegistry[pluginName] = layoutFull;
        workspace.topLevelTabs.push({
            id: "site",
            content: "Site",
            title: "View the documentation for Hawtio",
            isValid: function (workspace) { return false; },
            href: function () { return "#/site"; }
        });
    }]);
    hawtioPluginLoader.addModule(pluginName);
})(Site || (Site = {}));
var Site;
(function (Site) {
    Site._module.controller("Site.IndexController", ["$scope", "$location", function ($scope, $location) {
        $scope.slideInterval = 5000;
    }]);
})(Site || (Site = {}));
var Site;
(function (Site) {
    Site._module.controller("Site.PageController", ["$scope", "$routeParams", "$location", "$compile", "$http", "fileExtensionTypeRegistry", function ($scope, $routeParams, $location, $compile, $http, fileExtensionTypeRegistry) {
        var log = Logger.get("Site");
        var pageId = $routeParams["page"];
        if (!pageId) {
            pageId = "README.md";
        }
        if (!pageId.startsWith("/") && pageId.indexOf(":/") < 0 && pageId.indexOf("app/site/") < 0) {
            pageId = "app/site/" + pageId;
        }
        $scope.pageId = pageId;
        $scope.pageFolder = pageId.substring(0, pageId.lastIndexOf('/') + 1);
        log.info("Loading page '" + $scope.pageId + "'");
        $scope.getContents = function (filename, cb) {
            var fullPath = $scope.pageFolder + filename;
            log.info("Loading the contents of: " + fullPath);
            $http.get(fullPath).success(cb).error(function () { return cb(" "); });
        };
        $http.get($scope.pageId).success(onResults);
        function onResults(contents, status, headers, config) {
            $scope.contents = contents;
            $scope.html = contents;
            var format = Wiki.fileFormat($scope.pageId, fileExtensionTypeRegistry) || "markdown";
            if ("markdown" === format) {
                $scope.html = contents ? marked(contents) : "";
            }
            else if (format && format.startsWith("html")) {
                $scope.html = contents;
            }
            else {
            }
            $compile($scope.html)($scope);
            Core.$apply($scope);
        }
    }]);
})(Site || (Site = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioAutoColumns', function () {
        return new UI.AutoColumns();
    });
    var AutoColumns = (function () {
        function AutoColumns() {
            this.restrict = 'A';
            this.link = function ($scope, $element, $attr) {
                var selector = UI.getIfSet('hawtioAutoColumns', $attr, 'div');
                var minMargin = UI.getIfSet('minMargin', $attr, '3').toNumber();
                var go = Core.throttled(function () {
                    var containerWidth = $element.innerWidth();
                    var childWidth = 0;
                    var children = $element.children(selector);
                    if (children.length === 0) {
                        return;
                    }
                    children.each(function (child) {
                        var self = $(this);
                        if (!self.is(':visible')) {
                            return;
                        }
                        if (self.outerWidth() > childWidth) {
                            childWidth = self.outerWidth();
                        }
                    });
                    if (childWidth === 0) {
                        return;
                    }
                    childWidth = childWidth + (minMargin * 2);
                    var columns = Math.floor(containerWidth / childWidth);
                    if (children.length < columns) {
                        columns = children.length;
                    }
                    var margin = (containerWidth - (columns * childWidth)) / columns / 2;
                    children.each(function (child) {
                        $(this).css({
                            'margin-left': margin,
                            'margin-right': margin
                        });
                    });
                }, 500);
                setTimeout(go, 300);
                $scope.$watch(go);
                $(window).resize(go);
            };
        }
        return AutoColumns;
    })();
    UI.AutoColumns = AutoColumns;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioAutoDropdown', function () {
        return UI.AutoDropDown;
    });
    UI.AutoDropDown = {
        restrict: 'A',
        link: function ($scope, $element, $attrs) {
            function locateElements(event) {
                var el = $element.get(0);
                if (event && event.relatedNode !== el && event.type) {
                    if (event && event.type !== 'resize') {
                        return;
                    }
                }
                var overflowEl = $($element.find('.overflow'));
                var overflowMenu = $(overflowEl.find('ul.dropdown-menu'));
                var margin = 0;
                var availableWidth = 0;
                try {
                    margin = overflowEl.outerWidth() - overflowEl.innerWidth();
                    availableWidth = overflowEl.position().left - $element.position().left - 50;
                }
                catch (e) {
                    UI.log.debug("caught " + e);
                }
                $element.children('li:not(.overflow):not(.pull-right):not(:hidden)').each(function () {
                    var self = $(this);
                    availableWidth = availableWidth - self.outerWidth(true);
                    if (availableWidth < 0) {
                        self.detach();
                        self.prependTo(overflowMenu);
                    }
                });
                if (overflowMenu.children().length > 0) {
                    overflowEl.css({ visibility: "visible" });
                }
                if (availableWidth > 130) {
                    var noSpace = false;
                    overflowMenu.children('li:not(.overflow):not(.pull-right)').filter(function () {
                        return $(this).css('display') !== 'none';
                    }).each(function () {
                        if (noSpace) {
                            return;
                        }
                        var self = $(this);
                        if (availableWidth > self.outerWidth()) {
                            availableWidth = availableWidth - self.outerWidth();
                            self.detach();
                            self.insertBefore(overflowEl);
                        }
                        else {
                            noSpace = true;
                        }
                    });
                }
                if (overflowMenu.children().length === 0) {
                    overflowEl.css({ visibility: "hidden" });
                }
            }
            $(window).resize(locateElements);
            $element.get(0).addEventListener("DOMNodeInserted", locateElements);
            $scope.$watch(setTimeout(locateElements, 500));
        }
    };
})(UI || (UI = {}));
var UI;
(function (UI) {
    function hawtioBreadcrumbs() {
        return {
            restrict: 'E',
            replace: true,
            templateUrl: UI.templatePath + 'breadcrumbs.html',
            require: 'hawtioDropDown',
            scope: {
                config: '='
            },
            controller: ["$scope", "$element", "$attrs", function ($scope, $element, $attrs) {
                $scope.action = "itemClicked(config, $event)";
                $scope.levels = {};
                $scope.itemClicked = function (config, $event) {
                    if (config.level && angular.isNumber(config.level)) {
                        $scope.levels[config.level] = config;
                        var keys = Object.extended($scope.levels).keys().sortBy("");
                        var toRemove = keys.from(config.level + 1);
                        toRemove.forEach(function (i) {
                            if (i in $scope.levels) {
                                $scope.levels[i] = {};
                                delete $scope.levels[i];
                            }
                        });
                        angular.forEach($scope.levels, function (value, key) {
                            if (value.items && value.items.length > 0) {
                                value.items.forEach(function (i) {
                                    i['action'] = $scope.action;
                                });
                            }
                        });
                        if (config.items) {
                            config.open = true;
                            config.items.forEach(function (i) {
                                i['action'] = $scope.action;
                            });
                            delete config.action;
                        }
                        else {
                            var keys = Object.extended($scope.levels).keys().sortBy("");
                            var path = [];
                            keys.forEach(function (key) {
                                path.push($scope.levels[key]['title']);
                            });
                            var pathString = '/' + path.join("/");
                            $scope.config.path = pathString;
                        }
                        if (config.level > 1) {
                            $event.preventDefault();
                            $event.stopPropagation();
                        }
                    }
                };
                function addAction(config, level) {
                    config.level = level;
                    if (level > 0) {
                        config.breadcrumbAction = config.action;
                        config.action = $scope.action;
                    }
                    if (config.items) {
                        config.items.forEach(function (item) {
                            addAction(item, level + 1);
                        });
                    }
                }
                function setLevels(config, pathParts, level) {
                    if (pathParts.length === 0) {
                        return;
                    }
                    var part = pathParts.removeAt(0)[0];
                    if (config && config.items) {
                        var matched = false;
                        config.items.forEach(function (item) {
                            if (!matched && item['title'] === part) {
                                matched = true;
                                $scope.levels[level] = item;
                                setLevels(item, pathParts, level + 1);
                            }
                        });
                    }
                }
                $scope.$watch('config.path', function (newValue, oldValue) {
                    if (!Core.isBlank(newValue)) {
                        var pathParts = newValue.split('/').exclude(function (p) {
                            return Core.isBlank(p);
                        });
                        var matches = true;
                        pathParts.forEach(function (part, index) {
                            if (!matches) {
                                return;
                            }
                            if (!$scope.levels[index] || Core.isBlank($scope.levels[index]['title']) || $scope.levels[index]['title'] !== part) {
                                matches = false;
                            }
                        });
                        if (matches) {
                            return;
                        }
                        $scope.levels = [];
                        $scope.levels['0'] = $scope.config;
                        setLevels($scope.config, pathParts.from(0), 1);
                    }
                });
                $scope.$watch('config', function (newValue, oldValue) {
                    addAction($scope.config, 0);
                    $scope.levels['0'] = $scope.config;
                });
            }]
        };
    }
    UI.hawtioBreadcrumbs = hawtioBreadcrumbs;
    UI._module.directive('hawtioBreadcrumbs', UI.hawtioBreadcrumbs);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioColorPicker', function () {
        return new UI.ColorPicker();
    });
    UI.selected = "selected";
    UI.unselected = "unselected";
    var ColorPicker = (function () {
        function ColorPicker() {
            this.restrict = 'A';
            this.replace = true;
            this.scope = {
                property: '=hawtioColorPicker'
            };
            this.templateUrl = UI.templatePath + "colorPicker.html";
            this.compile = function (tElement, tAttrs, transclude) {
                return {
                    post: function postLink(scope, iElement, iAttrs, controller) {
                        scope.colorList = [];
                        angular.forEach(UI.colors, function (color) {
                            var select = UI.unselected;
                            if (scope.property === color) {
                                select = UI.selected;
                            }
                            scope.colorList.push({
                                color: color,
                                select: select
                            });
                        });
                    }
                };
            };
            this.controller = ["$scope", "$element", "$timeout", function ($scope, $element, $timeout) {
                $scope.popout = false;
                $scope.$watch('popout', function () {
                    $element.find('.color-picker-popout').toggleClass('popout-open', $scope.popout);
                });
                $scope.selectColor = function (color) {
                    for (var i = 0; i < $scope.colorList.length; i++) {
                        $scope.colorList[i].select = UI.unselected;
                        if ($scope.colorList[i] === color) {
                            $scope.property = color.color;
                            $scope.colorList[i].select = UI.selected;
                        }
                    }
                };
            }];
        }
        return ColorPicker;
    })();
    UI.ColorPicker = ColorPicker;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioConfirmDialog', function () {
        return new UI.ConfirmDialog();
    });
    var ConfirmDialog = (function () {
        function ConfirmDialog() {
            this.restrict = 'A';
            this.replace = true;
            this.transclude = true;
            this.templateUrl = UI.templatePath + 'confirmDialog.html';
            this.scope = {
                show: '=hawtioConfirmDialog',
                title: '@',
                okButtonText: '@',
                showOkButton: '@',
                cancelButtonText: '@',
                onCancel: '&?',
                onOk: '&?',
                onClose: '&?'
            };
            this.controller = ["$scope", "$element", "$attrs", "$transclude", "$compile", function ($scope, $element, $attrs, $transclude, $compile) {
                $scope.clone = null;
                $transclude(function (clone) {
                    $scope.clone = $(clone).filter('.dialog-body');
                });
                $scope.$watch('show', function () {
                    if ($scope.show) {
                        setTimeout(function () {
                            $scope.body = $('.modal-body');
                            $scope.body.html($compile($scope.clone.html())($scope.$parent));
                            Core.$apply($scope);
                        }, 50);
                    }
                });
                $attrs.$observe('okButtonText', function (value) {
                    if (!angular.isDefined(value)) {
                        $scope.okButtonText = "OK";
                    }
                });
                $attrs.$observe('cancelButtonText', function (value) {
                    if (!angular.isDefined(value)) {
                        $scope.cancelButtonText = "Cancel";
                    }
                });
                $attrs.$observe('title', function (value) {
                    if (!angular.isDefined(value)) {
                        $scope.title = "Are you sure?";
                    }
                });
                function checkClosed() {
                    setTimeout(function () {
                        var backdrop = $("div.modal-backdrop");
                        if (backdrop && backdrop.length) {
                            Logger.get("ConfirmDialog").debug("Removing the backdrop div! " + backdrop);
                            backdrop.remove();
                        }
                    }, 200);
                }
                $scope.cancel = function () {
                    $scope.show = false;
                    $scope.$parent.$eval($scope.onCancel);
                    checkClosed();
                };
                $scope.submit = function () {
                    $scope.show = false;
                    $scope.$parent.$eval($scope.onOk);
                    checkClosed();
                };
                $scope.close = function () {
                    $scope.$parent.$eval($scope.onClose);
                    checkClosed();
                };
            }];
        }
        return ConfirmDialog;
    })();
    UI.ConfirmDialog = ConfirmDialog;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.controller("UI.DeveloperPageController", ["$scope", "$http", function ($scope, $http) {
        $scope.getContents = function (filename, cb) {
            var fullUrl = "app/ui/html/test/" + filename;
            $http({ method: 'GET', url: fullUrl }).success(function (data, status, headers, config) {
                cb(data);
            }).error(function (data, status, headers, config) {
                cb("Failed to fetch " + filename + ": " + data);
            });
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI.hawtioDrag = UI._module.directive("hawtioDrag", [function () {
        return {
            replace: false,
            transclude: true,
            restrict: 'A',
            template: '<span ng-transclude></span>',
            scope: {
                data: '=hawtioDrag'
            },
            link: function (scope, element, attrs) {
                element.attr({
                    draggable: 'true'
                });
                var el = element[0];
                el.draggable = true;
                el.addEventListener('dragstart', function (event) {
                    event.dataTransfer.effectAllowed = 'move';
                    event.dataTransfer.setData('data', scope.data);
                    element.addClass('drag-started');
                    return false;
                }, false);
                el.addEventListener('dragend', function (event) {
                    element.removeClass('drag-started');
                }, false);
            }
        };
    }]);
    UI.hawtioDrop = UI._module.directive("hawtioDrop", [function () {
        return {
            replace: false,
            transclude: true,
            restrict: 'A',
            template: '<span ng-transclude></span>',
            scope: {
                onDrop: '&?hawtioDrop',
                ngModel: '=',
                property: '@',
                prefix: '@'
            },
            link: function (scope, element, attrs) {
                var dragEnter = function (event) {
                    if (event.preventDefault) {
                        event.preventDefault();
                    }
                    element.addClass('drag-over');
                    return false;
                };
                var el = element[0];
                el.addEventListener('dragenter', dragEnter, false);
                el.addEventListener('dragover', dragEnter, false);
                el.addEventListener('dragleave', function (event) {
                    element.removeClass('drag-over');
                    return false;
                }, false);
                el.addEventListener('drop', function (event) {
                    if (event.stopPropagation) {
                        event.stopPropagation();
                    }
                    element.removeClass('drag-over');
                    var data = event.dataTransfer.getData('data');
                    if (scope.onDrop) {
                        scope.$eval(scope.onDrop, {
                            data: data,
                            model: scope.ngModel,
                            property: scope.property
                        });
                    }
                    var eventName = 'hawtio-drop';
                    if (!Core.isBlank(scope.prefix)) {
                        eventName = scope.prefix + '-' + eventName;
                    }
                    scope.$emit(eventName, {
                        data: data,
                        model: scope.ngModel,
                        property: scope.property
                    });
                    Core.$apply(scope);
                    return false;
                }, false);
            }
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('editableProperty', ["$parse", function ($parse) {
        return new UI.EditableProperty($parse);
    }]);
    var EditableProperty = (function () {
        function EditableProperty($parse) {
            this.$parse = $parse;
            this.restrict = 'E';
            this.scope = true;
            this.templateUrl = UI.templatePath + 'editableProperty.html';
            this.require = 'ngModel';
            this.link = null;
            this.link = function (scope, element, attrs, ngModel) {
                scope.inputType = attrs['type'] || 'text';
                scope.min = attrs['min'];
                scope.max = attrs['max'];
                scope.getText = function () {
                    if (!scope.text) {
                        return '';
                    }
                    if (scope.inputType === 'password') {
                        return StringHelpers.obfusicate(scope.text);
                    }
                    else {
                        return scope.text;
                    }
                };
                scope.editing = false;
                $(element.find(".icon-pencil")[0]).hide();
                scope.getPropertyName = function () {
                    var propertyName = $parse(attrs['property'])(scope);
                    if (!propertyName && propertyName !== 0) {
                        propertyName = attrs['property'];
                    }
                    return propertyName;
                };
                ngModel.$render = function () {
                    if (!ngModel.$viewValue) {
                        return;
                    }
                    scope.text = ngModel.$viewValue[scope.getPropertyName()];
                };
                scope.getInputStyle = function () {
                    if (!scope.text) {
                        return {};
                    }
                    var calculatedWidth = (scope.text + "").length / 1.2;
                    if (calculatedWidth < 5) {
                        calculatedWidth = 5;
                    }
                    return {
                        width: calculatedWidth + 'em'
                    };
                };
                scope.showEdit = function () {
                    $(element.find(".icon-pencil")[0]).show();
                };
                scope.hideEdit = function () {
                    $(element.find(".icon-pencil")[0]).hide();
                };
                function inputSelector() {
                    return ':input[type=' + scope.inputType + ']';
                }
                scope.$watch('editing', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        if (newValue) {
                            $(element.find(inputSelector())).focus().select();
                        }
                    }
                });
                scope.doEdit = function () {
                    scope.editing = true;
                };
                scope.stopEdit = function () {
                    $(element.find(inputSelector())[0]).val(ngModel.$viewValue[scope.getPropertyName()]);
                    scope.editing = false;
                };
                scope.saveEdit = function () {
                    var value = $(element.find(inputSelector())[0]).val();
                    var obj = ngModel.$viewValue;
                    obj[scope.getPropertyName()] = value;
                    ngModel.$setViewValue(obj);
                    ngModel.$render();
                    scope.editing = false;
                    scope.$parent.$eval(attrs['onSave']);
                };
            };
        }
        return EditableProperty;
    })();
    UI.EditableProperty = EditableProperty;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioEditor', ["$parse", function ($parse) {
        return UI.Editor($parse);
    }]);
    function Editor($parse) {
        return {
            restrict: 'A',
            replace: true,
            templateUrl: UI.templatePath + "editor.html",
            scope: {
                text: '=hawtioEditor',
                mode: '=',
                outputEditor: '@',
                name: '@'
            },
            controller: ["$scope", "$element", "$attrs", function ($scope, $element, $attrs) {
                $scope.codeMirror = null;
                $scope.doc = null;
                $scope.options = [];
                UI.observe($scope, $attrs, 'name', 'editor');
                $scope.applyOptions = function () {
                    if ($scope.codeMirror) {
                        $scope.options.each(function (option) {
                            $scope.codeMirror.setOption(option.key, option['value']);
                        });
                        $scope.options = [];
                    }
                };
                $scope.$watch('doc', function () {
                    if ($scope.doc) {
                        $scope.codeMirror.on('change', function (changeObj) {
                            $scope.text = $scope.doc.getValue();
                            $scope.dirty = !$scope.doc.isClean();
                            Core.$apply($scope);
                        });
                    }
                });
                $scope.$watch('codeMirror', function () {
                    if ($scope.codeMirror) {
                        $scope.doc = $scope.codeMirror.getDoc();
                    }
                });
                $scope.$watch('text', function (oldValue, newValue) {
                    if ($scope.codeMirror && $scope.doc) {
                        if (!$scope.codeMirror.hasFocus()) {
                            var text = $scope.text || "";
                            if (angular.isArray(text) || angular.isObject(text)) {
                                text = JSON.stringify(text, null, "  ");
                                $scope.mode = "javascript";
                                $scope.codeMirror.setOption("mode", "javascript");
                            }
                            $scope.doc.setValue(text);
                        }
                    }
                });
            }],
            link: function ($scope, $element, $attrs) {
                if ('dirty' in $attrs) {
                    $scope.dirtyTarget = $attrs['dirty'];
                    $scope.$watch("$parent['" + $scope.dirtyTarget + "']", function (newValue, oldValue) {
                        if (newValue !== oldValue) {
                            $scope.dirty = newValue;
                        }
                    });
                }
                var config = Object.extended($attrs).clone();
                delete config['$$element'];
                delete config['$attr'];
                delete config['class'];
                delete config['hawtioEditor'];
                delete config['mode'];
                delete config['dirty'];
                delete config['outputEditor'];
                if ('onChange' in $attrs) {
                    var onChange = $attrs['onChange'];
                    delete config['onChange'];
                    $scope.options.push({
                        onChange: function (codeMirror) {
                            var func = $parse(onChange);
                            if (func) {
                                func($scope.$parent, { codeMirror: codeMirror });
                            }
                        }
                    });
                }
                angular.forEach(config, function (value, key) {
                    $scope.options.push({
                        key: key,
                        'value': value
                    });
                });
                $scope.$watch('mode', function () {
                    if ($scope.mode) {
                        if (!$scope.codeMirror) {
                            $scope.options.push({
                                key: 'mode',
                                'value': $scope.mode
                            });
                        }
                        else {
                            $scope.codeMirror.setOption('mode', $scope.mode);
                        }
                    }
                });
                $scope.$watch('dirty', function (newValue, oldValue) {
                    if ($scope.dirty && !$scope.doc.isClean()) {
                        $scope.doc.markClean();
                    }
                    if (newValue !== oldValue && 'dirtyTarget' in $scope) {
                        $scope.$parent[$scope.dirtyTarget] = $scope.dirty;
                    }
                });
                $scope.$watch(function () {
                    return $element.is(':visible');
                }, function (newValue, oldValue) {
                    if (newValue !== oldValue && $scope.codeMirror) {
                        $scope.codeMirror.refresh();
                    }
                });
                $scope.$watch('text', function () {
                    if (!$scope.codeMirror) {
                        var options = {
                            value: $scope.text
                        };
                        options = CodeEditor.createEditorSettings(options);
                        $scope.codeMirror = CodeMirror.fromTextArea($element.find('textarea').get(0), options);
                        var outputEditor = $scope.outputEditor;
                        if (outputEditor) {
                            var outputScope = $scope.$parent || $scope;
                            Core.pathSet(outputScope, outputEditor, $scope.codeMirror);
                        }
                        $scope.applyOptions();
                    }
                });
            }
        };
    }
    UI.Editor = Editor;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('expandable', function () {
        return new UI.Expandable();
    });
    var Expandable = (function () {
        function Expandable() {
            var _this = this;
            this.log = Logger.get("Expandable");
            this.restrict = 'C';
            this.replace = false;
            this.link = null;
            this.link = function (scope, element, attrs) {
                var self = _this;
                var expandable = element;
                var modelName = null;
                var model = null;
                if (angular.isDefined(attrs['model'])) {
                    modelName = attrs['model'];
                    model = scope[modelName];
                    if (!angular.isDefined(scope[modelName]['expanded'])) {
                        model['expanded'] = expandable.hasClass('opened');
                    }
                    else {
                        if (model['expanded']) {
                            self.forceOpen(model, expandable, scope);
                        }
                        else {
                            self.forceClose(model, expandable, scope);
                        }
                    }
                    if (modelName) {
                        scope.$watch(modelName + '.expanded', function (newValue, oldValue) {
                            if (asBoolean(newValue) !== asBoolean(oldValue)) {
                                if (newValue) {
                                    self.open(model, expandable, scope);
                                }
                                else {
                                    self.close(model, expandable, scope);
                                }
                            }
                        });
                    }
                }
                var title = expandable.find('.title');
                var button = expandable.find('.cancel');
                button.bind('click', function () {
                    model = scope[modelName];
                    self.forceClose(model, expandable, scope);
                    return false;
                });
                title.bind('click', function () {
                    model = scope[modelName];
                    if (isOpen(expandable)) {
                        self.close(model, expandable, scope);
                    }
                    else {
                        self.open(model, expandable, scope);
                    }
                    return false;
                });
            };
        }
        Expandable.prototype.open = function (model, expandable, scope) {
            expandable.find('.expandable-body').slideDown(400, function () {
                if (!expandable.hasClass('opened')) {
                    expandable.addClass('opened');
                }
                expandable.removeClass('closed');
                if (model) {
                    model['expanded'] = true;
                }
                Core.$apply(scope);
            });
        };
        Expandable.prototype.close = function (model, expandable, scope) {
            expandable.find('.expandable-body').slideUp(400, function () {
                expandable.removeClass('opened');
                if (!expandable.hasClass('closed')) {
                    expandable.addClass('closed');
                }
                if (model) {
                    model['expanded'] = false;
                }
                Core.$apply(scope);
            });
        };
        Expandable.prototype.forceClose = function (model, expandable, scope) {
            expandable.find('.expandable-body').slideUp(0, function () {
                if (!expandable.hasClass('closed')) {
                    expandable.addClass('closed');
                }
                expandable.removeClass('opened');
                if (model) {
                    model['expanded'] = false;
                }
                Core.$apply(scope);
            });
        };
        Expandable.prototype.forceOpen = function (model, expandable, scope) {
            expandable.find('.expandable-body').slideDown(0, function () {
                if (!expandable.hasClass('opened')) {
                    expandable.addClass('opened');
                }
                expandable.removeClass('closed');
                if (model) {
                    model['expanded'] = true;
                }
                Core.$apply(scope);
            });
        };
        return Expandable;
    })();
    UI.Expandable = Expandable;
    function isOpen(expandable) {
        return expandable.hasClass('opened') || !expandable.hasClass("closed");
    }
    function asBoolean(value) {
        return value ? true : false;
    }
})(UI || (UI = {}));
var UI;
(function (UI) {
    var hawtioFileDrop = UI._module.directive("hawtioFileDrop", [function () {
        return {
            restrict: 'A',
            replace: false,
            link: function (scope, element, attr) {
                var fileName = attr['hawtioFileDrop'];
                var downloadURL = attr['downloadUrl'];
                var mimeType = attr['mimeType'] || 'application/octet-stream';
                if (Core.isBlank(fileName) || Core.isBlank(downloadURL)) {
                    return;
                }
                if (!downloadURL.startsWith("http")) {
                    var uri = new URI();
                    downloadURL = uri.path(downloadURL).toString();
                }
                var fileDetails = mimeType + ":" + fileName + ":" + downloadURL;
                element.attr({
                    draggable: true
                });
                element[0].addEventListener("dragstart", function (event) {
                    if (event.dataTransfer) {
                        UI.log.debug("Drag started, event: ", event, "File details: ", fileDetails);
                        event.dataTransfer.setData("DownloadURL", fileDetails);
                    }
                    else {
                        UI.log.debug("Drag event object doesn't contain data transfer: ", event);
                    }
                });
            }
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI.hawtioFilter = UI._module.directive("hawtioFilter", [function () {
        return {
            restrict: 'E',
            replace: true,
            transclude: true,
            templateUrl: UI.templatePath + 'filter.html',
            scope: {
                placeholder: '@',
                cssClass: '@',
                saveAs: '@?',
                ngModel: '='
            },
            controller: ["$scope", "localStorage", "$location", "$element", function ($scope, localStorage, $location, $element) {
                $scope.getClass = function () {
                    var answer = [];
                    if (!Core.isBlank($scope.cssClass)) {
                        answer.push($scope.cssClass);
                    }
                    if (!Core.isBlank($scope.ngModel)) {
                        answer.push("has-text");
                    }
                    return answer.join(' ');
                };
                if (!Core.isBlank($scope.saveAs)) {
                    if ($scope.saveAs in localStorage) {
                        var val = localStorage[$scope.saveAs];
                        if (!Core.isBlank(val)) {
                            $scope.ngModel = val;
                        }
                        else {
                            $scope.ngModel = '';
                        }
                    }
                    else {
                        $scope.ngModel = '';
                    }
                    var updateFunc = function () {
                        localStorage[$scope.saveAs] = $scope.ngModel;
                    };
                    $scope.$watch('ngModel', updateFunc);
                }
            }]
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('gridster', function () {
        return new UI.GridsterDirective();
    });
    var GridsterDirective = (function () {
        function GridsterDirective() {
            this.restrict = 'A';
            this.replace = true;
            this.controller = ["$scope", "$element", "$attrs", function ($scope, $element, $attrs) {
            }];
            this.link = function ($scope, $element, $attrs) {
                var widgetMargins = [6, 6];
                var widgetBaseDimensions = [150, 150];
                var gridSize = [150, 150];
                var extraRows = 10;
                var extraCols = 6;
                if (angular.isDefined($attrs['extraRows'])) {
                    extraRows = $attrs['extraRows'].toNumber();
                }
                if (angular.isDefined($attrs['extraCols'])) {
                    extraCols = $attrs['extraCols'].toNumber();
                }
                var grid = $('<ul style="margin: 0"></ul>');
                var styleStr = '<style type="text/css">';
                var styleStr = styleStr + '</style>';
                $element.append($(styleStr));
                $element.append(grid);
                $scope.gridster = grid.gridster({
                    widget_margins: widgetMargins,
                    grid_size: gridSize,
                    extra_rows: extraRows,
                    extra_cols: extraCols
                }).data('gridster');
            };
        }
        return GridsterDirective;
    })();
    UI.GridsterDirective = GridsterDirective;
})(UI || (UI = {}));
var UI;
(function (UI) {
    function groupBy() {
        return function (list, group) {
            if (list.length === 0) {
                return list;
            }
            if (Core.isBlank(group)) {
                return list;
            }
            var newGroup = 'newGroup';
            var endGroup = 'endGroup';
            var currentGroup = undefined;
            function createNewGroup(list, item, index) {
                item[newGroup] = true;
                item[endGroup] = false;
                currentGroup = item[group];
                if (index > 0) {
                    list[index - 1][endGroup] = true;
                }
            }
            function addItemToExistingGroup(item) {
                item[newGroup] = false;
                item[endGroup] = false;
            }
            list.forEach(function (item, index) {
                var createGroup = item[group] !== currentGroup;
                if (angular.isArray(item[group])) {
                    if (currentGroup === undefined) {
                        createGroup = true;
                    }
                    else {
                        var targetGroup = item[group];
                        if (targetGroup.length !== currentGroup.length) {
                            createGroup = true;
                        }
                        else {
                            createGroup = false;
                            targetGroup.forEach(function (item) {
                                if (!createGroup && !currentGroup.any(function (i) {
                                    return i === item;
                                })) {
                                    createGroup = true;
                                }
                            });
                        }
                    }
                }
                if (createGroup) {
                    createNewGroup(list, item, index);
                }
                else {
                    addItemToExistingGroup(item);
                }
            });
            return list;
        };
    }
    UI.groupBy = groupBy;
    UI._module.filter('hawtioGroupBy', UI.groupBy);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI.IconTestController = UI._module.controller("UI.IconTestController", ["$scope", "$templateCache", function ($scope, $templateCache) {
        $scope.exampleHtml = $templateCache.get('example-html');
        $scope.exampleConfigJson = $templateCache.get('example-config-json');
        $scope.$watch('exampleConfigJson', function (newValue, oldValue) {
            $scope.icons = angular.fromJson($scope.exampleConfigJson);
        });
    }]);
    function hawtioIcon() {
        UI.log.debug("Creating icon directive");
        return {
            restrict: 'E',
            replace: true,
            templateUrl: UI.templatePath + 'icon.html',
            scope: {
                icon: '=config'
            },
            link: function ($scope, $element, $attrs) {
                if (!$scope.icon) {
                    return;
                }
                if (!('type' in $scope.icon) && !Core.isBlank($scope.icon.src)) {
                    if ($scope.icon.src.startsWith("icon-")) {
                        $scope.icon.type = "icon";
                    }
                    else {
                        $scope.icon.type = "img";
                    }
                }
            }
        };
    }
    UI.hawtioIcon = hawtioIcon;
    UI._module.directive('hawtioIcon', UI.hawtioIcon);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioJsplumb', ["$timeout", "$window", function ($timeout, $window) {
        return {
            restrict: 'A',
            link: function ($scope, $element, $attrs) {
                $window.addEventListener("resize", function () {
                    if ($scope.jsPlumb) {
                        $scope.jsPlumb.recalculateOffsets($element);
                        $scope.jsPlumb.repaintEverything();
                    }
                });
                var enableDragging = true;
                if (angular.isDefined($attrs['draggable'])) {
                    enableDragging = Core.parseBooleanValue($attrs['draggable']);
                }
                var useLayout = true;
                if (angular.isDefined($attrs['layout'])) {
                    useLayout = Core.parseBooleanValue($attrs['layout']);
                }
                var direction = 'TB';
                if (angular.isDefined($attrs['direction'])) {
                    switch ($attrs['direction']) {
                        case 'down':
                            direction = 'LR';
                            break;
                        default:
                            direction = 'TB';
                    }
                }
                var nodeSep = 50;
                var edgeSep = 10;
                var rankSep = 50;
                if (angular.isDefined($attrs['nodeSep'])) {
                    nodeSep = Core.parseIntValue($attrs['nodeSep']);
                }
                if (angular.isDefined($attrs['edgeSep'])) {
                    edgeSep = Core.parseIntValue($attrs['edgeSep']);
                }
                if (angular.isDefined($attrs['rankSep'])) {
                    rankSep = Core.parseIntValue($attrs['rankSep']);
                }
                var timeout = 100;
                if (angular.isDefined($attrs['timeout'])) {
                    timeout = Core.parseIntValue($attrs['timeout'], "timeout");
                }
                var endpointStyle = ["Dot", { radius: 10, cssClass: 'jsplumb-circle', hoverClass: 'jsplumb-circle-hover' }];
                var labelStyles = ["Label"];
                var arrowStyles = ["Arrow", {
                    location: 1,
                    id: "arrow",
                    length: 8,
                    width: 8,
                    foldback: 0.8
                }];
                var connectorStyle = ["Flowchart", { cornerRadius: 4, gap: 8 }];
                if (angular.isDefined($scope.connectorStyle)) {
                    connectorStyle = $scope.connectorStyle;
                }
                function createNode(nodeEl) {
                    var el = $(nodeEl);
                    var id = el.attr('id');
                    var anchors = el.attr('anchors');
                    if (!Core.isBlank(anchors) && (anchors.has("{{") || anchors.has("}}"))) {
                        return null;
                    }
                    if (!Core.isBlank(anchors)) {
                        anchors = anchors.split(',').map(function (anchor) {
                            return anchor.trim();
                        });
                    }
                    else {
                        anchors = ["Continuous"];
                    }
                    var node = {
                        id: id,
                        label: 'node ' + id,
                        el: el,
                        width: el.outerWidth(),
                        height: el.outerHeight(),
                        edges: [],
                        connections: [],
                        endpoints: [],
                        anchors: anchors
                    };
                    return node;
                }
                ;
                function createEndpoint(jsPlumb, node) {
                    var options = {
                        isSource: true,
                        isTarget: true,
                        anchor: node.anchors,
                        connector: connectorStyle,
                        maxConnections: -1
                    };
                    if (angular.isFunction($scope.customizeEndpointOptions)) {
                        $scope.customizeEndpointOptions(jsPlumb, node, options);
                    }
                    var endpoint = jsPlumb.addEndpoint(node.el, options);
                    node.endpoints.push(endpoint);
                    if (enableDragging) {
                        jsPlumb.draggable(node.el, {
                            containment: $element
                        });
                    }
                }
                ;
                var nodes = [];
                var transitions = [];
                var nodesById = {};
                function gatherElements() {
                    var nodeEls = $element.find('.jsplumb-node');
                    if (nodes.length > 0) {
                    }
                    angular.forEach(nodeEls, function (nodeEl) {
                        if (!nodesById[nodeEl.id]) {
                            var node = createNode(nodeEl);
                            if (node) {
                                nodes.push(node);
                                nodesById[node.id] = node;
                            }
                        }
                    });
                    angular.forEach(nodes, function (sourceNode) {
                        var targets = sourceNode.el.attr('connect-to');
                        if (targets) {
                            targets = targets.split(',');
                            angular.forEach(targets, function (target) {
                                var targetNode = nodesById[target.trim()];
                                if (targetNode) {
                                    var edge = {
                                        source: sourceNode,
                                        target: targetNode
                                    };
                                    transitions.push(edge);
                                    sourceNode.edges.push(edge);
                                    targetNode.edges.push(edge);
                                }
                            });
                        }
                    });
                }
                ;
                $scope.$on('jsplumbDoWhileSuspended', function (event, op) {
                    if ($scope.jsPlumb) {
                        var jsPlumb = $scope.jsPlumb;
                        jsPlumb.doWhileSuspended(function () {
                            UI.log.debug("Suspended jsplumb");
                            $scope.jsPlumb.reset();
                            op();
                            nodes = [];
                            nodesById = {};
                            transitions = [];
                            go();
                        });
                    }
                });
                function go() {
                    if (!$scope.jsPlumb) {
                        $scope.jsPlumb = jsPlumb.getInstance({
                            Container: $element
                        });
                        var defaultOptions = {
                            Anchor: "AutoDefault",
                            Connector: "Flowchart",
                            ConnectorStyle: connectorStyle,
                            DragOptions: { cursor: "pointer", zIndex: 2000 },
                            Endpoint: endpointStyle,
                            PaintStyle: { strokeStyle: "#42a62c", lineWidth: 4 },
                            HoverPaintStyle: { strokeStyle: "#42a62c", lineWidth: 4 },
                            ConnectionOverlays: [
                                arrowStyles,
                                labelStyles
                            ]
                        };
                        if (!enableDragging) {
                            defaultOptions['ConnectionsDetachable'] = false;
                        }
                        if (angular.isFunction($scope.customizeDefaultOptions)) {
                            $scope.customizeDefaultOptions(defaultOptions);
                        }
                        $scope.jsPlumb.importDefaults(defaultOptions);
                    }
                    gatherElements();
                    $scope.jsPlumbNodes = nodes;
                    $scope.jsPlumbNodesById = nodesById;
                    $scope.jsPlumbTransitions = transitions;
                    if (useLayout) {
                        $scope.layout = dagre.layout().nodeSep(nodeSep).edgeSep(edgeSep).rankSep(rankSep).rankDir(direction).nodes(nodes).edges(transitions).run();
                    }
                    angular.forEach($scope.jsPlumbNodes, function (node) {
                        if (useLayout) {
                            var divWidth = node.el.width();
                            var divHeight = node.el.height();
                            var y = node.dagre.y - (divHeight / 2);
                            var x = node.dagre.x - (divWidth / 2);
                            node.el.css({ top: y, left: x });
                        }
                        createEndpoint($scope.jsPlumb, node);
                    });
                    angular.forEach($scope.jsPlumbTransitions, function (edge) {
                        var options = {
                            connector: connectorStyle,
                            maxConnections: -1
                        };
                        var params = {
                            source: edge.source.el,
                            target: edge.target.el
                        };
                        if (angular.isFunction($scope.customizeConnectionOptions)) {
                            $scope.customizeConnectionOptions($scope.jsPlumb, edge, params, options);
                        }
                        var connection = $scope.jsPlumb.connect(params, options);
                        edge.source.connections.push(connection);
                        edge.target.connections.push(connection);
                    });
                    $scope.jsPlumb.recalculateOffsets($element);
                    if (!$scope.jsPlumb.isSuspendDrawing()) {
                        $scope.jsPlumb.repaintEverything();
                    }
                    if (angular.isDefined($scope.jsPlumbCallback) && angular.isFunction($scope.jsPlumbCallback)) {
                        $scope.jsPlumbCallback($scope.jsPlumb, $scope.jsPlumbNodes, $scope.jsPlumbNodesById, $scope.jsPlumbTransitions);
                    }
                }
                $timeout(go, timeout);
            }
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    function hawtioList($templateCache, $compile) {
        return {
            restrict: '',
            replace: true,
            templateUrl: UI.templatePath + 'list.html',
            scope: {
                'config': '=hawtioList'
            },
            link: function ($scope, $element, $attr) {
                $scope.rows = [];
                $scope.name = "hawtioListScope";
                if (!$scope.config.selectedItems) {
                    $scope.config.selectedItems = [];
                }
                $scope.$watch('rows', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        $scope.config.selectedItems.length = 0;
                        var selected = $scope.rows.findAll(function (row) {
                            return row.selected;
                        });
                        selected.forEach(function (row) {
                            $scope.config.selectedItems.push(row.entity);
                        });
                    }
                }, true);
                $scope.cellTemplate = $templateCache.get('cellTemplate.html');
                $scope.rowTemplate = $templateCache.get('rowTemplate.html');
                var columnDefs = $scope.config['columnDefs'];
                var fieldName = 'name';
                var displayName = 'Name';
                if (columnDefs && columnDefs.length > 0) {
                    var def = columnDefs.first();
                    fieldName = def['field'] || fieldName;
                    displayName = def['displayName'] || displayName;
                    if (def['cellTemplate']) {
                        $scope.cellTemplate = def['cellTemplate'];
                    }
                }
                var configName = $attr['hawtioList'];
                var dataName = $scope.config['data'];
                if (Core.isBlank(configName) || Core.isBlank(dataName)) {
                    return;
                }
                $scope.listRoot = function () {
                    return $element.find('.list-root');
                };
                $scope.getContents = function (row) {
                    var innerScope = $scope.$new();
                    innerScope.row = row;
                    var rowEl = $compile($scope.rowTemplate)(innerScope);
                    var innerParentScope = $scope.parentScope.$new();
                    innerParentScope.row = row;
                    innerParentScope.col = {
                        field: fieldName
                    };
                    var cellEl = $compile($scope.cellTemplate)(innerParentScope);
                    $(rowEl).find('.list-row-contents').append(cellEl);
                    return rowEl;
                };
                $scope.setRows = function (data) {
                    $scope.rows = [];
                    var list = $scope.listRoot();
                    list.empty();
                    if (data) {
                        data.forEach(function (row) {
                            var newRow = {
                                entity: row,
                                getProperty: function (name) {
                                    if (!angular.isDefined(name)) {
                                        return null;
                                    }
                                    return row[name];
                                }
                            };
                            list.append($scope.getContents(newRow));
                            $scope.rows.push(newRow);
                        });
                    }
                };
                var parentScope = UI.findParentWith($scope, configName);
                if (parentScope) {
                    $scope.parentScope = parentScope;
                    parentScope.$watch(dataName, $scope.setRows, true);
                }
            }
        };
    }
    UI.hawtioList = hawtioList;
    UI._module.directive('hawtioList', ["$templateCache", "$compile", UI.hawtioList]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    var objectView = UI._module.directive("hawtioObject", ["$templateCache", "$interpolate", "$compile", function ($templateCache, $interpolate, $compile) {
        return {
            restrict: "A",
            replace: true,
            templateUrl: UI.templatePath + "object.html",
            scope: {
                "entity": "=?hawtioObject",
                "config": "=?",
                "path": "=?",
                "row": "=?"
            },
            link: function ($scope, $element, $attr) {
                function interpolate(template, path, key, value) {
                    var interpolateFunc = $interpolate(template);
                    if (!key) {
                        return interpolateFunc({
                            data: value,
                            path: path
                        });
                    }
                    else {
                        return interpolateFunc({
                            key: key.titleize(),
                            data: value,
                            path: path
                        });
                    }
                }
                function getEntityConfig(path, config) {
                    var answer = undefined;
                    var properties = Core.pathGet(config, ['properties']);
                    if (!answer && properties) {
                        angular.forEach(properties, function (config, propertySelector) {
                            var regex = new RegExp(propertySelector);
                            if (regex.test(path)) {
                                if (answer && !answer.override && !config.override) {
                                    answer = Object.merge(answer, config);
                                }
                                else {
                                    answer = Object.clone(config, true);
                                }
                            }
                        });
                    }
                    return answer;
                }
                function getTemplate(path, config, def) {
                    var answer = def;
                    var config = getEntityConfig(path, config);
                    if (config && config.template) {
                        answer = config.template;
                    }
                    return answer;
                }
                function compile(template, path, key, value, config) {
                    var config = getEntityConfig(path, config);
                    if (config && config.hidden) {
                        return;
                    }
                    var interpolated = null;
                    if (config && config.template) {
                        interpolated = config.template;
                    }
                    else {
                        interpolated = interpolate(template, path, key, value);
                    }
                    var scope = $scope.$new();
                    scope.row = $scope.row;
                    scope.entityConfig = config;
                    scope.data = value;
                    scope.path = path;
                    return $compile(interpolated)(scope);
                }
                function renderPrimitiveValue(path, entity, config) {
                    var template = getTemplate(path, config, $templateCache.get('primitiveValueTemplate.html'));
                    return compile(template, path, undefined, entity, config);
                }
                function renderDateValue(path, entity, config) {
                    var template = getTemplate(path, config, $templateCache.get('dateValueTemplate.html'));
                    return compile(template, path, undefined, entity, config);
                }
                function renderObjectValue(path, entity, config) {
                    var isArray = false;
                    var el = undefined;
                    angular.forEach(entity, function (value, key) {
                        if (angular.isNumber(key) && "length" in entity) {
                            isArray = true;
                        }
                        if (isArray) {
                            return;
                        }
                        if (key.startsWith("$")) {
                            return;
                        }
                        if (!el) {
                            el = angular.element('<span></span>');
                        }
                        if (angular.isArray(value)) {
                            el.append(renderArrayAttribute(path + '/' + key, key, value, config));
                        }
                        else if (angular.isObject(value)) {
                            if (Object.extended(value).size() === 0) {
                                el.append(renderPrimitiveAttribute(path + '/' + key, key, 'empty', config));
                            }
                            else {
                                el.append(renderObjectAttribute(path + '/' + key, key, value, config));
                            }
                        }
                        else if (StringHelpers.isDate(value)) {
                            el.append(renderDateAttribute(path + '/' + key, key, Date.create(value), config));
                        }
                        else {
                            el.append(renderPrimitiveAttribute(path + '/' + key, key, value, config));
                        }
                    });
                    if (el) {
                        return el.children();
                    }
                    else {
                        return el;
                    }
                }
                function getColumnHeaders(path, entity, config) {
                    var answer = undefined;
                    if (!entity) {
                        return answer;
                    }
                    var hasPrimitive = false;
                    entity.forEach(function (item) {
                        if (!hasPrimitive && angular.isObject(item)) {
                            if (!answer) {
                                answer = [];
                            }
                            answer = Object.extended(item).keys().filter(function (key) {
                                return !angular.isFunction(item[key]);
                            }).filter(function (key) {
                                var conf = getEntityConfig(path + '/' + key, config);
                                if (conf && conf.hidden) {
                                    return false;
                                }
                                return true;
                            }).union(answer);
                        }
                        else {
                            answer = undefined;
                            hasPrimitive = true;
                        }
                    });
                    if (answer) {
                        answer = answer.exclude(function (item) {
                            return ("" + item).startsWith('$');
                        });
                    }
                    return answer;
                }
                function renderTable(template, path, key, value, headers, config) {
                    var el = angular.element(interpolate(template, path, key, value));
                    var thead = el.find('thead');
                    var tbody = el.find('tbody');
                    var headerTemplate = $templateCache.get('headerTemplate.html');
                    var cellTemplate = $templateCache.get('cellTemplate.html');
                    var rowTemplate = $templateCache.get('rowTemplate.html');
                    var headerRow = angular.element(interpolate(rowTemplate, path, undefined, undefined));
                    headers.forEach(function (header) {
                        headerRow.append(interpolate(headerTemplate, path + '/' + header, header, undefined));
                    });
                    thead.append(headerRow);
                    value.forEach(function (item, index) {
                        var tr = angular.element(interpolate(rowTemplate, path + '/' + index, undefined, undefined));
                        headers.forEach(function (header) {
                            var td = angular.element(interpolate(cellTemplate, path + '/' + index + '/' + header, undefined, undefined));
                            td.append(renderThing(path + '/' + index + '/' + header, item[header], config));
                            tr.append(td);
                        });
                        tbody.append(tr);
                    });
                    return el;
                }
                function renderArrayValue(path, entity, config) {
                    var headers = getColumnHeaders(path, entity, config);
                    if (!headers) {
                        var template = getTemplate(path, config, $templateCache.get('arrayValueListTemplate.html'));
                        return compile(template, path, undefined, entity, config);
                    }
                    else {
                        var template = getTemplate(path, config, $templateCache.get('arrayValueTableTemplate.html'));
                        return renderTable(template, path, undefined, entity, headers, config);
                    }
                }
                function renderPrimitiveAttribute(path, key, value, config) {
                    var template = getTemplate(path, config, $templateCache.get('primitiveAttributeTemplate.html'));
                    return compile(template, path, key, value, config);
                }
                function renderDateAttribute(path, key, value, config) {
                    var template = getTemplate(path, config, $templateCache.get('dateAttributeTemplate.html'));
                    return compile(template, path, key, value, config);
                }
                function renderObjectAttribute(path, key, value, config) {
                    var template = getTemplate(path, config, $templateCache.get('objectAttributeTemplate.html'));
                    return compile(template, path, key, value, config);
                }
                function renderArrayAttribute(path, key, value, config) {
                    var headers = getColumnHeaders(path, value, config);
                    if (!headers) {
                        var template = getTemplate(path, config, $templateCache.get('arrayAttributeListTemplate.html'));
                        return compile(template, path, key, value, config);
                    }
                    else {
                        var template = getTemplate(path, config, $templateCache.get('arrayAttributeTableTemplate.html'));
                        return renderTable(template, path, key, value, headers, config);
                    }
                }
                function renderThing(path, entity, config) {
                    if (angular.isArray(entity)) {
                        return renderArrayValue(path, entity, config);
                    }
                    else if (angular.isObject(entity)) {
                        return renderObjectValue(path, entity, config);
                    }
                    else if (StringHelpers.isDate(entity)) {
                        return renderDateValue(path, Date.create(entity), config);
                    }
                    else {
                        return renderPrimitiveValue(path, entity, config);
                    }
                }
                $scope.$watch('entity', function (entity) {
                    if (!entity) {
                        $element.empty();
                        return;
                    }
                    if (!$scope.path) {
                        $scope.path = "";
                    }
                    if (!angular.isDefined($scope.row)) {
                        $scope.row = {
                            entity: entity
                        };
                    }
                    $element.html(renderThing($scope.path, entity, $scope.config));
                }, true);
            }
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    function hawtioPane() {
        return {
            restrict: 'E',
            replace: true,
            transclude: true,
            templateUrl: UI.templatePath + 'pane.html',
            scope: {
                position: '@',
                width: '@',
                header: '@'
            },
            controller: ["$scope", "$element", "$attrs", "$transclude", "$document", "$timeout", "$compile", "$templateCache", "$window", function ($scope, $element, $attrs, $transclude, $document, $timeout, $compile, $templateCache, $window) {
                $scope.moving = false;
                $transclude(function (clone) {
                    $element.find(".pane-content").append(clone);
                    if (Core.isBlank($scope.header)) {
                        return;
                    }
                    var headerTemplate = $templateCache.get($scope.header);
                    var wrapper = $element.find(".pane-header-wrapper");
                    wrapper.html($compile(headerTemplate)($scope));
                    $timeout(function () {
                        $element.find(".pane-viewport").css("top", wrapper.height());
                    }, 500);
                });
                $scope.setViewportTop = function () {
                    var wrapper = $element.find(".pane-header-wrapper");
                    $timeout(function () {
                        $element.find(".pane-viewport").css("top", wrapper.height());
                    }, 10);
                };
                $scope.setWidth = function (width) {
                    if (width < 6) {
                        return;
                    }
                    $element.width(width);
                    $element.parent().css($scope.padding, $element.width() + "px");
                    $scope.setViewportTop();
                };
                $scope.open = function () {
                    $scope.setWidth($scope.width);
                };
                $scope.close = function () {
                    $scope.width = $element.width();
                    $scope.setWidth(6);
                };
                $scope.$on('pane.close', $scope.close);
                $scope.$on('pane.open', $scope.open);
                $scope.toggle = function () {
                    if ($scope.moving) {
                        return;
                    }
                    if ($element.width() > 6) {
                        $scope.close();
                    }
                    else {
                        $scope.open();
                    }
                };
                $scope.startMoving = function ($event) {
                    $event.stopPropagation();
                    $event.preventDefault();
                    $event.stopImmediatePropagation();
                    $document.on("mouseup.hawtio-pane", function ($event) {
                        $timeout(function () {
                            $scope.moving = false;
                        }, 250);
                        $event.stopPropagation();
                        $event.preventDefault();
                        $event.stopImmediatePropagation();
                        $document.off(".hawtio-pane");
                        Core.$apply($scope);
                    });
                    $document.on("mousemove.hawtio-pane", function ($event) {
                        $scope.moving = true;
                        $event.stopPropagation();
                        $event.preventDefault();
                        $event.stopImmediatePropagation();
                        if ($scope.position === 'left') {
                            $scope.setWidth($event.pageX + 2);
                        }
                        else {
                            $scope.setWidth($window.innerWidth - $event.pageX + 2);
                        }
                        Core.$apply($scope);
                    });
                };
            }],
            link: function ($scope, $element, $attr) {
                var parent = $element.parent();
                var position = "left";
                if ($scope.position) {
                    position = $scope.position;
                }
                $element.addClass(position);
                var width = $element.width();
                var padding = "padding-" + position;
                $scope.padding = padding;
                var original = parent.css(padding);
                parent.css(padding, width + "px");
                $scope.$on('$destroy', function () {
                    parent.css(padding, original);
                });
            }
        };
    }
    UI.hawtioPane = hawtioPane;
    UI._module.directive('hawtioPane', UI.hawtioPane);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioMessagePanel', function () {
        return new UI.MessagePanel();
    });
    var MessagePanel = (function () {
        function MessagePanel() {
            this.restrict = 'A';
            this.link = function ($scope, $element, $attrs) {
                var height = "100%";
                if ('hawtioMessagePanel' in $attrs) {
                    var wantedHeight = $attrs['hawtioMessagePanel'];
                    if (wantedHeight && !wantedHeight.isBlank()) {
                        height = wantedHeight;
                    }
                }
                var speed = "1s";
                if ('speed' in $attrs) {
                    var wantedSpeed = $attrs['speed'];
                    if (speed && !speed.isBlank()) {
                        speed = wantedSpeed;
                    }
                }
                $element.css({
                    position: 'absolute',
                    bottom: 0,
                    height: 0,
                    'min-height': 0,
                    transition: 'all ' + speed + ' ease-in-out'
                });
                $element.parent().mouseover(function () {
                    $element.css({
                        height: height,
                        'min-height': 'auto'
                    });
                });
                $element.parent().mouseout(function () {
                    $element.css({
                        height: 0,
                        'min-height': 0
                    });
                });
            };
        }
        return MessagePanel;
    })();
    UI.MessagePanel = MessagePanel;
    UI._module.directive('hawtioInfoPanel', function () {
        return new UI.InfoPanel();
    });
    var InfoPanel = (function () {
        function InfoPanel() {
            this.restrict = 'A';
            this.link = function ($scope, $element, $attrs) {
                var validDirections = {
                    'left': {
                        side: 'right',
                        out: 'width'
                    },
                    'right': {
                        side: 'left',
                        out: 'width'
                    },
                    'up': {
                        side: 'bottom',
                        out: 'height'
                    },
                    'down': {
                        side: 'top',
                        out: 'height'
                    }
                };
                var direction = "right";
                if ('hawtioInfoPanel' in $attrs) {
                    var wantedDirection = $attrs['hawtioInfoPanel'];
                    if (wantedDirection && !wantedDirection.isBlank()) {
                        if (Object.extended(validDirections).keys().any(wantedDirection)) {
                            direction = wantedDirection;
                        }
                    }
                }
                var speed = "1s";
                if ('speed' in $attrs) {
                    var wantedSpeed = $attrs['speed'];
                    if (speed && !speed.isBlank()) {
                        speed = wantedSpeed;
                    }
                }
                var toggle = "open";
                if ('toggle' in $attrs) {
                    var wantedToggle = $attrs['toggle'];
                    if (toggle && !toggle.isBlank()) {
                        toggle = wantedToggle;
                    }
                }
                var initialCss = {
                    position: 'absolute',
                    transition: 'all ' + speed + ' ease-in-out'
                };
                var openCss = {};
                openCss[validDirections[direction]['out']] = '100%';
                var closedCss = {};
                closedCss[validDirections[direction]['out']] = 0;
                initialCss[validDirections[direction]['side']] = 0;
                initialCss[validDirections[direction]['out']] = 0;
                $element.css(initialCss);
                $scope.$watch(toggle, function (newValue, oldValue) {
                    if (Core.parseBooleanValue(newValue)) {
                        $element.css(openCss);
                    }
                    else {
                        $element.css(closedCss);
                    }
                });
                $element.click(function () {
                    $scope[toggle] = false;
                    Core.$apply($scope);
                });
            };
        }
        return InfoPanel;
    })();
    UI.InfoPanel = InfoPanel;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioRow', function () {
        return new UI.DivRow();
    });
    var DivRow = (function () {
        function DivRow() {
            this.restrict = 'A';
            this.link = function ($scope, $element, $attrs) {
                $element.get(0).addEventListener("DOMNodeInserted", function () {
                    var targets = $element.children();
                    var width = 0;
                    angular.forEach(targets, function (target) {
                        var el = angular.element(target);
                        switch (el.css('display')) {
                            case 'none':
                                break;
                            default:
                                width = width + el.outerWidth(true) + 5;
                        }
                    });
                    $element.width(width);
                });
            };
        }
        return DivRow;
    })();
    UI.DivRow = DivRow;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioSlideout', function () {
        return new UI.SlideOut();
    });
    var SlideOut = (function () {
        function SlideOut() {
            this.restrict = 'A';
            this.replace = true;
            this.transclude = true;
            this.templateUrl = UI.templatePath + 'slideout.html';
            this.scope = {
                show: '=hawtioSlideout',
                direction: '@',
                top: '@',
                height: '@',
                title: '@'
            };
            this.controller = ["$scope", "$element", "$attrs", "$transclude", "$compile", function ($scope, $element, $attrs, $transclude, $compile) {
                $scope.clone = null;
                $transclude(function (clone) {
                    $scope.clone = $(clone).filter('.dialog-body');
                });
                UI.observe($scope, $attrs, 'direction', 'right');
                UI.observe($scope, $attrs, 'top', '10%', function (value) {
                    $element.css('top', value);
                });
                UI.observe($scope, $attrs, 'height', '80%', function (value) {
                    $element.css('height', value);
                });
                UI.observe($scope, $attrs, 'title', '');
                $scope.$watch('show', function () {
                    if ($scope.show) {
                        $scope.body = $element.find('.slideout-body');
                        $scope.body.html($compile($scope.clone.html())($scope.$parent));
                    }
                });
                $scope.hidePanel = function ($event) {
                    UI.log.debug("Event: ", $event);
                    $scope.show = false;
                };
            }];
            this.link = function ($scope, $element, $attrs) {
                $scope.$watch('show', function () {
                    if ($scope.show) {
                        $element.addClass('out');
                        $element.focus();
                    }
                    else {
                        $element.removeClass('out');
                    }
                });
            };
        }
        return SlideOut;
    })();
    UI.SlideOut = SlideOut;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioPager', function () {
        return new UI.TablePager();
    });
    var TablePager = (function () {
        function TablePager() {
            var _this = this;
            this.restrict = 'A';
            this.scope = true;
            this.templateUrl = UI.templatePath + 'tablePager.html';
            this.$scope = null;
            this.element = null;
            this.attrs = null;
            this.tableName = null;
            this.setRowIndexName = null;
            this.rowIndexName = null;
            this.link = function (scope, element, attrs) {
                return _this.doLink(scope, element, attrs);
            };
        }
        TablePager.prototype.doLink = function (scope, element, attrs) {
            var _this = this;
            this.$scope = scope;
            this.element = element;
            this.attrs = attrs;
            this.tableName = attrs["hawtioPager"] || attrs["array"] || "data";
            this.setRowIndexName = attrs["onIndexChange"] || "onIndexChange";
            this.rowIndexName = attrs["rowIndex"] || "rowIndex";
            scope.first = function () {
                _this.goToIndex(0);
            };
            scope.last = function () {
                _this.goToIndex(scope.tableLength() - 1);
            };
            scope.previous = function () {
                _this.goToIndex(scope.rowIndex() - 1);
            };
            scope.next = function () {
                _this.goToIndex(scope.rowIndex() + 1);
            };
            scope.isEmptyOrFirst = function () {
                var idx = scope.rowIndex();
                var length = scope.tableLength();
                return length <= 0 || idx <= 0;
            };
            scope.isEmptyOrLast = function () {
                var idx = scope.rowIndex();
                var length = scope.tableLength();
                return length < 1 || idx + 1 >= length;
            };
            scope.rowIndex = function () {
                return Core.pathGet(scope.$parent, _this.rowIndexName.split('.'));
            };
            scope.tableLength = function () {
                var data = _this.tableData();
                return data ? data.length : 0;
            };
        };
        TablePager.prototype.tableData = function () {
            return Core.pathGet(this.$scope.$parent, this.tableName.split('.')) || [];
        };
        TablePager.prototype.goToIndex = function (idx) {
            var name = this.setRowIndexName;
            var fn = this.$scope[name];
            if (angular.isFunction(fn)) {
                fn(idx);
            }
            else {
                console.log("No function defined in scope for " + name + " but was " + fn);
                this.$scope[this.rowIndexName] = idx;
            }
        };
        return TablePager;
    })();
    UI.TablePager = TablePager;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI.hawtioTagFilter = UI._module.directive("hawtioTagFilter", [function () {
        return {
            restrict: 'E',
            replace: true,
            templateUrl: UI.templatePath + 'tagFilter.html',
            scope: {
                selected: '=',
                tags: '=',
                collection: '=?',
                collectionProperty: '@',
                saveAs: '@'
            },
            controller: ["$scope", "localStorage", "$location", function ($scope, localStorage, $location) {
                SelectionHelpers.decorate($scope);
                if (!Core.isBlank($scope.saveAs)) {
                    var search = $location.search();
                    if ($scope.saveAs in search) {
                        $scope.selected.add(angular.fromJson(search[$scope.saveAs]));
                    }
                    else if ($scope.saveAs in localStorage) {
                        $scope.selected.add(angular.fromJson(localStorage[$scope.saveAs]));
                    }
                }
                function maybeFilterVisibleTags() {
                    if ($scope.collection && $scope.collectionProperty) {
                        if (!$scope.selected.length) {
                            $scope.visibleTags = $scope.tags;
                            $scope.filteredCollection = $scope.collection;
                        }
                        else {
                            filterVisibleTags();
                        }
                        $scope.visibleTags = $scope.visibleTags.map(function (t) {
                            return {
                                id: t,
                                count: $scope.filteredCollection.map(function (c) {
                                    return c[$scope.collectionProperty];
                                }).reduce(function (count, c) {
                                    if (c.any(t)) {
                                        return count + 1;
                                    }
                                    return count;
                                }, 0)
                            };
                        });
                    }
                    else {
                        $scope.visibleTags = $scope.tags;
                    }
                }
                function filterVisibleTags() {
                    $scope.filteredCollection = $scope.collection.filter(function (c) {
                        return SelectionHelpers.filterByGroup($scope.selected, c[$scope.collectionProperty]);
                    });
                    $scope.visibleTags = [];
                    $scope.filteredCollection.forEach(function (c) {
                        $scope.visibleTags = $scope.visibleTags.union(c[$scope.collectionProperty]);
                    });
                }
                $scope.$watch('tags', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        SelectionHelpers.syncGroupSelection($scope.selected, $scope.tags);
                        maybeFilterVisibleTags();
                    }
                });
                $scope.$watch('selected', function (newValue, oldValue) {
                    if (!Core.isBlank($scope.saveAs)) {
                        var saveAs = angular.toJson($scope.selected);
                        localStorage[$scope.saveAs] = saveAs;
                        $location.search($scope.saveAs, saveAs);
                    }
                    maybeFilterVisibleTags();
                }, true);
            }]
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI.hawtioTagList = UI._module.directive("hawtioTagList", ['$interpolate', '$compile', function ($interpolate, $compile) {
        return {
            restrict: 'E',
            replace: true,
            scope: {
                ngModel: '=?',
                property: '@',
                onChange: '&'
            },
            link: function (scope, $element, attr) {
                if (!scope.ngModel || !scope.property || !scope.ngModel[scope.property]) {
                    return;
                }
                scope.collection = scope.ngModel[scope.property];
                scope.removeTag = function (tag) {
                    scope.ngModel[scope.property].remove(tag);
                    if (scope.onChange) {
                        scope.$eval(scope.onChange);
                    }
                };
                scope.$watch('collection', function (newValue, oldValue) {
                    if (!scope.ngModel || !scope.property || !scope.ngModel[scope.property]) {
                        return;
                    }
                    var tags = scope.ngModel[scope.property];
                    var tmp = angular.element("<div></div>");
                    tags.forEach(function (tag) {
                        var func = $interpolate('<span class="badge badge-success mouse-pointer">{{tag}} <i class="icon-remove" ng-click="removeTag(\'{{tag}}\')"></i></span>&nbsp;');
                        tmp.append(func({
                            tag: tag
                        }));
                    });
                    $element.html($compile(tmp.children())(scope));
                }, true);
            }
        };
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    function TemplatePopover($templateCache, $compile, $document) {
        return {
            restrict: 'A',
            link: function ($scope, $element, $attr) {
                var title = UI.getIfSet('title', $attr, undefined);
                var trigger = UI.getIfSet('trigger', $attr, 'hover');
                var html = true;
                var contentTemplate = UI.getIfSet('content', $attr, 'popoverTemplate');
                var placement = UI.getIfSet('placement', $attr, 'auto');
                var delay = UI.getIfSet('delay', $attr, '100');
                var container = UI.getIfSet('container', $attr, 'body');
                var selector = UI.getIfSet('selector', $attr, 'false');
                if (container === 'false') {
                    container = false;
                }
                if (selector === 'false') {
                    selector = false;
                }
                var template = $templateCache.get(contentTemplate);
                if (!template) {
                    return;
                }
                $element.on('$destroy', function () {
                    $element.popover('destroy');
                });
                $element.popover({
                    title: title,
                    trigger: trigger,
                    html: html,
                    content: function () {
                        var res = $compile(template)($scope);
                        Core.$digest($scope);
                        return res;
                    },
                    delay: delay,
                    container: container,
                    selector: selector,
                    placement: function (tip, element) {
                        if (placement !== 'auto') {
                            return placement;
                        }
                        var el = $element;
                        var offset = el.offset();
                        var width = $document.innerWidth();
                        var elHorizontalCenter = offset['left'] + (el.outerWidth() / 2);
                        var midpoint = width / 2;
                        if (elHorizontalCenter < midpoint) {
                            return 'right';
                        }
                        else {
                            return 'left';
                        }
                    }
                });
            }
        };
    }
    UI.TemplatePopover = TemplatePopover;
    UI._module.directive('hawtioTemplatePopover', ["$templateCache", "$compile", "$document", UI.TemplatePopover]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.controller("UI.UITestController2", ["$scope", "$templateCache", function ($scope, $templateCache) {
        $scope.fileUploadExMode = 'text/html';
        $scope.menuItems = [];
        $scope.divs = [];
        for (var i = 0; i < 20; i++) {
            $scope.menuItems.push("Some Item " + i);
        }
        for (var i = 0; i < 20; i++) {
            $scope.divs.push(i + 1);
        }
        $scope.things = [
            {
                'name': 'stuff1',
                'foo1': 'bar1',
                'foo2': 'bar2'
            },
            {
                'name': 'stuff2',
                'foo3': 'bar3',
                'foo4': 'bar4'
            }
        ];
        $scope.someVal = 1;
        $scope.dropDownConfig = {
            icon: 'icon-cogs',
            title: 'My Awesome Menu',
            items: [{
                title: 'Some Item',
                action: 'someVal=2'
            }, {
                title: 'Some other stuff',
                icon: 'icon-twitter',
                action: 'someVal=3'
            }, {
                title: "I've got children",
                icon: 'icon-file-text',
                items: [{
                    title: 'Hi!',
                    action: 'someVal=4'
                }, {
                    title: 'Yo!',
                    items: [{
                        title: 'More!',
                        action: 'someVal=5'
                    }, {
                        title: 'Child',
                        action: 'someVal=6'
                    }, {
                        title: 'Menus!',
                        action: 'someVal=7'
                    }]
                }]
            }, {
                title: "Call a function!",
                action: function () {
                    Core.notification("info", "Function called!");
                }
            }]
        };
        $scope.dropDownConfigTxt = angular.toJson($scope.dropDownConfig, true);
        $scope.$watch('dropDownConfigTxt', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $scope.dropDownConfig = angular.fromJson($scope.dropDownConfigTxt);
            }
        });
        $scope.breadcrumbSelection = 1;
        $scope.breadcrumbConfig = {
            path: '/root/first child',
            icon: 'icon-cogs',
            title: 'root',
            items: [{
                title: 'first child',
                icon: 'icon-folder-close-alt',
                items: [{
                    title: "first child's first child",
                    icon: 'icon-file-text'
                }]
            }, {
                title: 'second child',
                icon: 'icon-file'
            }, {
                title: "third child",
                icon: 'icon-folder-close-alt',
                items: [{
                    title: "third child's first child",
                    icon: 'icon-file-text'
                }, {
                    title: "third child's second child",
                    icon: 'icon-file-text'
                }, {
                    title: "third child's third child",
                    icon: 'icon-folder-close-alt',
                    items: [{
                        title: 'More!',
                        icon: 'icon-file-text'
                    }, {
                        title: 'Child',
                        icon: 'icon-file-text'
                    }, {
                        title: 'Menus!',
                        icon: 'icon-file-text'
                    }]
                }]
            }]
        };
        $scope.breadcrumbConfigTxt = angular.toJson($scope.breadcrumbConfig, true);
        $scope.$watch('breadcrumbConfigTxt', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                $scope.breadcrumbconfig = angular.toJson($scope.breadcrumbConfigTxt);
            }
        });
        $scope.breadcrumbEx = $templateCache.get("breadcrumbTemplate");
        $scope.dropDownEx = $templateCache.get("dropDownTemplate");
        $scope.autoDropDown = $templateCache.get("autoDropDownTemplate");
        $scope.zeroClipboard = $templateCache.get("zeroClipboardTemplate");
        $scope.popoverEx = $templateCache.get("myTemplate");
        $scope.popoverUsageEx = $templateCache.get("popoverExTemplate");
        $scope.autoColumnEx = $templateCache.get("autoColumnTemplate");
    }]);
    UI._module.controller("UI.UITestController1", ["$scope", "$templateCache", function ($scope, $templateCache) {
        $scope.jsplumbEx = $templateCache.get("jsplumbTemplate");
        $scope.nodes = ["node1", "node2"];
        $scope.otherNodes = ["node4", "node5", "node6"];
        $scope.anchors = ["Top", "Right", "Bottom", "Left"];
        $scope.createEndpoint = function (nodeId) {
            var node = $scope.jsPlumbNodesById[nodeId];
            if (node) {
                var anchors = $scope.anchors.subtract(node.anchors);
                console.log("anchors: ", anchors);
                if (anchors && anchors.length > 0) {
                    var anchor = anchors.first();
                    node.anchors.push(anchor);
                    node.endpoints.push($scope.jsPlumb.addEndpoint(node.el, {
                        anchor: anchor,
                        isSource: true,
                        isTarget: true,
                        maxConnections: -1
                    }));
                }
            }
        };
        $scope.expandableEx = '' + '<div class="expandable closed">\n' + '   <div title="The title" class="title">\n' + '     <i class="expandable-indicator"></i> Expandable title\n' + '   </div>\n' + '   <div class="expandable-body well">\n' + '     This is the expandable content...  Note that adding the "well" class isn\'t necessary but makes for a nice inset look\n' + '   </div>\n' + '</div>';
        $scope.editablePropertyEx1 = '<editable-property ng-model="editablePropertyModelEx1" property="property"></editable-property>';
        $scope.editablePropertyModelEx1 = {
            property: "This is editable (hover to edit)"
        };
        $scope.showDeleteOne = new UI.Dialog();
        $scope.showDeleteTwo = new UI.Dialog();
        $scope.fileUploadEx1 = '<div hawtio-file-upload="files" target="test1"></div>';
        $scope.fileUploadEx2 = '<div hawtio-file-upload="files" target="test2" show-files="false"></div>';
        $scope.fileUploadExMode = 'text/html';
        $scope.colorPickerEx = 'My Color ({{myColor}}): <div hawtio-color-picker="myColor"></div>';
        $scope.confirmationEx1 = '' + '<button class="btn" ng-click="showDeleteOne.open()">Delete stuff</button>\n' + '\n' + '<div hawtio-confirm-dialog="showDeleteOne.show"\n' + 'title="Delete stuff?"\n' + 'ok-button-text="Yes, Delete the Stuff"\n' + 'cancel-button-text="No, Keep the Stuff"\n' + 'on-cancel="onCancelled(\'One\')"\n' + 'on-ok="onOk(\'One\')">\n' + '  <div class="dialog-body">\n' + '    <p>\n' + '        Are you sure you want to delete all the stuff?\n' + '    </p>\n' + '  </div>\n' + '</div>\n';
        $scope.confirmationEx2 = '' + '<button class="btn" ng-click="showDeleteTwo.open()">Delete other stuff</button>\n' + '\n' + '<!-- Use more defaults -->\n' + '<div hawtio-confirm-dialog="showDeleteTwo.show\n"' + '  on-cancel="onCancelled(\'Two\')"\n' + '  on-ok="onOk(\'Two\')">\n' + '  <div class="dialog-body">\n' + '    <p>\n' + '      Are you sure you want to delete all the other stuff?\n' + '    </p>\n' + '  </div>\n' + '</div>';
        $scope.sliderEx1 = '' + '<button class="btn" ng-click="showSlideoutRight = !showSlideoutRight">Show slideout right</button>\n' + '<div hawtio-slideout="showSlideoutRight" title="Hey look a slider!">\n' + '   <div class="dialog-body">\n' + '     <div>\n' + '       Here is some content or whatever {{transcludedValue}}\n' + '     </div>\n' + '   </div>\n' + '</div>';
        $scope.sliderEx2 = '' + '<button class="btn" ng-click="showSlideoutLeft = !showSlideoutLeft">Show slideout left</button>\n' + '<div hawtio-slideout="showSlideoutLeft" direction="left" title="Hey, another slider!">\n' + '   <div class="dialog-body">\n' + '     <div hawtio-editor="someText" mode="javascript"></div>\n' + '   </div>\n' + '</div>\n';
        $scope.editorEx1 = '' + 'Instance 1\n' + '<div class="row-fluid">\n' + '   <div hawtio-editor="someText" mode="mode" dirty="dirty"></div>\n' + '   <div>Text : {{someText}}</div>\n' + '</div>\n' + '\n' + 'Instance 2 (readonly)\n' + '<div class="row-fluid">\n' + '   <div hawtio-editor="someText" read-only="true" mode="mode" dirty="dirty"></div>\n' + '   <div>Text : {{someText}}</div>\n' + '</div>';
        $scope.transcludedValue = "and this is transcluded";
        $scope.onCancelled = function (number) {
            Core.notification('info', 'cancelled ' + number);
        };
        $scope.onOk = function (number) {
            Core.notification('info', number + ' ok!');
        };
        $scope.showSlideoutRight = false;
        $scope.showSlideoutLeft = false;
        $scope.dirty = false;
        $scope.mode = 'javascript';
        $scope.someText = "var someValue = 0;\n" + "var someFunc = function() {\n" + "  return \"Hello World!\";\n" + "}\n";
        $scope.myColor = "#FF887C";
        $scope.showColorDialog = false;
        $scope.files = [];
        $scope.$watch('files', function (newValue, oldValue) {
            if (newValue !== oldValue) {
                console.log("Files: ", $scope.files);
            }
        }, true);
    }]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    function HawtioTocDisplay(marked, $location, $anchorScroll, $compile) {
        var log = Logger.get("UI");
        return {
            restrict: 'A',
            scope: {
                getContents: '&'
            },
            controller: ["$scope", "$element", "$attrs", function ($scope, $element, $attrs) {
                $scope.remaining = -1;
                $scope.render = false;
                $scope.chapters = [];
                $scope.addChapter = function (item) {
                    console.log("Adding: ", item);
                    $scope.chapters.push(item);
                    if (!angular.isDefined(item['text'])) {
                        $scope.fetchItemContent(item);
                    }
                };
                $scope.getTarget = function (id) {
                    if (!id) {
                        return '';
                    }
                    return id.replace(".", "_");
                };
                $scope.getFilename = function (href, ext) {
                    var filename = href.split('/').last();
                    if (ext && !filename.endsWith(ext)) {
                        filename = filename + '.' + ext;
                    }
                    return filename;
                };
                $scope.$watch('remaining', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        var renderIfPageLoadFails = false;
                        if (newValue === 0 || renderIfPageLoadFails) {
                            $scope.render = true;
                        }
                    }
                });
                $scope.fetchItemContent = function (item) {
                    var me = $scope;
                    $scope.$eval(function (parent) {
                        parent.getContents({
                            filename: item['filename'],
                            cb: function (data) {
                                if (data) {
                                    if (item['filename'].endsWith(".md")) {
                                        item['text'] = marked(data);
                                    }
                                    else {
                                        item['text'] = data;
                                    }
                                    $scope.remaining--;
                                    Core.$apply(me);
                                }
                            }
                        });
                    });
                };
            }],
            link: function ($scope, $element, $attrs) {
                var offsetTop = 0;
                var logbar = $('.logbar');
                var contentDiv = $("#toc-content");
                if (logbar.length) {
                    offsetTop = logbar.height() + logbar.offset().top;
                }
                else if (contentDiv.length) {
                    var offsetContentDiv = contentDiv.offset();
                    if (offsetContentDiv) {
                        offsetTop = offsetContentDiv.top;
                    }
                }
                if (!offsetTop) {
                    offsetTop = 90;
                }
                var previousHtml = null;
                var html = $element;
                if (!contentDiv || !contentDiv.length) {
                    contentDiv = $element;
                }
                var ownerScope = $scope.$parent || $scope;
                var scrollDuration = 1000;
                var linkFilter = $attrs["linkFilter"];
                var htmlName = $attrs["html"];
                if (htmlName) {
                    ownerScope.$watch(htmlName, function () {
                        var htmlText = ownerScope[htmlName];
                        if (htmlText && htmlText !== previousHtml) {
                            previousHtml = htmlText;
                            var markup = $compile(htmlText)(ownerScope);
                            $element.children().remove();
                            $element.append(markup);
                            loadChapters();
                        }
                    });
                }
                else {
                    loadChapters();
                }
                $(window).scroll(setFirstChapterActive);
                function setFirstChapterActive() {
                    var cutoff = $(window).scrollTop();
                    $element.find("li a").removeClass("active");
                    $('.panel-body').each(function () {
                        var offset = $(this).offset();
                        if (offset && offset.top >= cutoff) {
                            var id = $(this).attr("id");
                            if (id) {
                                var link = html.find("a[chapter-id='" + id + "']");
                                link.addClass("active");
                                return false;
                            }
                        }
                    });
                }
                function findLinks() {
                    var answer = html.find('a');
                    if (linkFilter) {
                        answer = answer.filter(linkFilter);
                    }
                    return answer;
                }
                function loadChapters() {
                    if (!html.get(0).id) {
                        html.get(0).id = 'toc';
                    }
                    $scope.tocId = '#' + html.get(0).id;
                    $scope.remaining = findLinks().length;
                    findLinks().each(function (index, a) {
                        log.debug("Found: ", a);
                        var filename = $scope.getFilename(a.href, a.getAttribute('file-extension'));
                        var item = {
                            filename: filename,
                            title: a.textContent,
                            link: a
                        };
                        $scope.addChapter(item);
                    });
                    setTimeout(function () {
                        setFirstChapterActive();
                    }, 100);
                }
                $scope.$watch('render', function (newValue, oldValue) {
                    if (newValue !== oldValue) {
                        if (newValue) {
                            if (!contentDiv.next('.hawtio-toc').length) {
                                var div = $('<div class="hawtio-toc"></div>');
                                div.appendTo(contentDiv);
                                var selectedChapter = $location.search()["chapter"];
                                $scope.chapters.forEach(function (chapter, index) {
                                    log.debug("index:", index);
                                    var panel = $('<div></div>');
                                    var panelHeader = null;
                                    var chapterId = $scope.getTarget(chapter['filename']);
                                    var link = chapter["link"];
                                    if (link) {
                                        link.setAttribute("chapter-id", chapterId);
                                    }
                                    if (index > 0) {
                                        panelHeader = $('<div class="panel-title"><a class="toc-back" href="">Back to Top</a></div>');
                                    }
                                    var panelBody = $('<div class="panel-body" id="' + chapterId + '">' + chapter['text'] + '</div>');
                                    if (panelHeader) {
                                        panel.append(panelHeader).append($compile(panelBody)($scope));
                                    }
                                    else {
                                        panel.append($compile(panelBody)($scope));
                                    }
                                    panel.hide().appendTo(div).fadeIn(1000);
                                    if (chapterId === selectedChapter) {
                                        scrollToChapter(chapterId);
                                    }
                                });
                                var pageTop = contentDiv.offset().top - offsetTop;
                                div.find('a.toc-back').each(function (index, a) {
                                    $(a).click(function (e) {
                                        e.preventDefault();
                                        $('body,html').animate({
                                            scrollTop: pageTop
                                        }, 2000);
                                    });
                                });
                                findLinks().each(function (index, a) {
                                    var href = a.href;
                                    var filename = $scope.getFilename(href, a.getAttribute('file-extension'));
                                    $(a).click(function (e) {
                                        log.debug("Clicked: ", e);
                                        e.preventDefault();
                                        var chapterId = $scope.getTarget(filename);
                                        $location.search("chapter", chapterId);
                                        Core.$apply(ownerScope);
                                        scrollToChapter(chapterId);
                                        return true;
                                    });
                                });
                            }
                        }
                    }
                });
                ownerScope.$on("$locationChangeSuccess", function (event, current, previous) {
                    setTimeout(function () {
                        var currentChapter = $location.search()["chapter"];
                        scrollToChapter(currentChapter);
                    }, 50);
                });
                function scrollToChapter(chapterId) {
                    log.debug("selected chapter changed: " + chapterId);
                    if (chapterId) {
                        var target = '#' + chapterId;
                        var top = 0;
                        var targetElements = $(target);
                        if (targetElements.length) {
                            var offset = targetElements.offset();
                            if (offset) {
                                top = offset.top - offsetTop;
                            }
                            $('body,html').animate({
                                scrollTop: top
                            }, scrollDuration);
                        }
                    }
                }
            }
        };
    }
    UI.HawtioTocDisplay = HawtioTocDisplay;
    UI._module.directive('hawtioTocDisplay', ["marked", "$location", "$anchorScroll", "$compile", UI.HawtioTocDisplay]);
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('hawtioViewport', function () {
        return new UI.ViewportHeight();
    });
    var ViewportHeight = (function () {
        function ViewportHeight() {
            this.restrict = 'A';
            this.link = function ($scope, $element, $attrs) {
                var lastHeight = 0;
                var resizeFunc = function () {
                    var neighbor = angular.element($attrs['hawtioViewport']);
                    var container = angular.element($attrs['containingDiv']);
                    var start = neighbor.position().top + neighbor.height();
                    var myHeight = container.height() - start;
                    if (angular.isDefined($attrs['heightAdjust'])) {
                        var heightAdjust = $attrs['heightAdjust'].toNumber();
                    }
                    myHeight = myHeight + heightAdjust;
                    $element.css({
                        height: myHeight,
                        'min-height': myHeight
                    });
                    if (lastHeight !== myHeight) {
                        lastHeight = myHeight;
                        $element.trigger('resize');
                    }
                };
                resizeFunc();
                $scope.$watch(resizeFunc);
                $().resize(function () {
                    resizeFunc();
                    Core.$apply($scope);
                    return false;
                });
            };
        }
        return ViewportHeight;
    })();
    UI.ViewportHeight = ViewportHeight;
    UI._module.directive('hawtioHorizontalViewport', function () {
        return new UI.HorizontalViewport();
    });
    var HorizontalViewport = (function () {
        function HorizontalViewport() {
            this.restrict = 'A';
            this.link = function ($scope, $element, $attrs) {
                var adjustParent = angular.isDefined($attrs['adjustParent']) && Core.parseBooleanValue($attrs['adjustParent']);
                $element.get(0).addEventListener("DOMNodeInserted", function () {
                    var canvas = $element.children();
                    $element.height(canvas.outerHeight(true));
                    if (adjustParent) {
                        $element.parent().height($element.outerHeight(true) + UI.getScrollbarWidth());
                    }
                });
            };
        }
        return HorizontalViewport;
    })();
    UI.HorizontalViewport = HorizontalViewport;
})(UI || (UI = {}));
var UI;
(function (UI) {
    UI._module.directive('zeroClipboard', ["$parse", function ($parse) {
        return UI.ZeroClipboardDirective($parse);
    }]);
    function ZeroClipboardDirective($parse) {
        return {
            restrict: 'A',
            link: function ($scope, $element, $attr) {
                var clip = new window.ZeroClipboard($element.get(0), {
                    moviePath: "img/ZeroClipboard.swf"
                });
                clip.on('complete', function (client, args) {
                    if (args.text && angular.isString(args.text)) {
                        Core.notification('info', "Copied text to clipboard: " + args.text.truncate(20));
                    }
                    Core.$apply($scope);
                });
                if ('useCallback' in $attr) {
                    var func = $parse($attr['useCallback']);
                    if (func) {
                        func($scope, { clip: clip });
                    }
                }
            }
        };
    }
    UI.ZeroClipboardDirective = ZeroClipboardDirective;
})(UI || (UI = {}));
//# sourceMappingURL=app.js.map
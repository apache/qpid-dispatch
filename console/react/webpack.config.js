const fs = require('fs');
const path = require('path');
const HtmlWebPackPlugin = require("html-webpack-plugin");

const appDirectory = fs.realpathSync(process.cwd());
const resolveApp = relativePath => path.resolve(appDirectory, relativePath);

module.exports = {
    mode: 'development',
    entry: './test_demo/index.js',
    output: {
        path: path.resolve(__dirname, 'dist'),
        filename: 'bundle.js',
    },
    optimization: {
        runtimeChunk: true,
        removeAvailableModules: false,
        removeEmptyChunks: false,
        splitChunks: false,
    },
    // devtool: 'inline-source-map',
    devtool: 'eval-cheap-module-source-map',
    devServer: {
        contentBase: path.join(__dirname, 'public'),
        watchContentBase: true,
        contentBasePublicPath: '',
        hot: true,
        compress: true,
        port: 9000,
    },
    module: {
        rules: [
            {
                test: /\.(js|jsx)$/,
                // include: path.resolve(__dirname, 'src'),
                exclude: /node_modules/,
                use: {
                    loader: "babel-loader"
                }
            },
            {
                test: /\.css$/,
                use: ['style-loader', "css-loader"]
            },
            {
                test: /\.(png|svg|jpg|gif)$/,
                use: [
                    'file-loader',
                ],
            },
            {
                test: /\.(woff(2)?|ttf|eot|svg)(\?v=\d+\.\d+\.\d+)?$/,
                use: [
                    {
                        loader: 'file-loader',
                        // options: {
                        //     name: '[name].[ext]',
                        //     outputPath: 'fonts/'
                        // }
                    }
                ]
            },
            // {
            //     test: /\.json$/,
            //     loader: 'file-loader',
            //     options: {
            //         context: path.resolve(__dirname, 'public')
            //     },
            // },
        ]
    },
    plugins: [
        new HtmlWebPackPlugin({
            template: "./test_demo/index_.html",
            filename: "./index.html"
        })
    ]
};

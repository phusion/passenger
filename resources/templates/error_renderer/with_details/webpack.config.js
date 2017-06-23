const path = require('path');
const ExtractTextPlugin = require('extract-text-webpack-plugin');
const UglifyJSPlugin = require('uglifyjs-webpack-plugin');

const extractCSS = new ExtractTextPlugin('styles.css');

module.exports = {
  entry: ['./src/index.js'],
  output: {
    path: path.resolve(__dirname, 'dist'),
    filename: 'bundle.js'
  },
  module: {
    rules: [
      {
        test: /\.(js|jsx)$/,
        use: {
          loader: 'babel-loader',
          options: {
            presets: ['env', 'react']
          }
        }
      },
      {
        test: /\.css$/,
        use: extractCSS.extract({
          use: {
            loader: 'css-loader',
            options: {
              minimize: true
            }
          },
          fallback: 'style-loader'
        })
      },
    ]
  },
  plugins: [
    extractCSS,
    new UglifyJSPlugin()
  ]
};

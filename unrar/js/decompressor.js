// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * A class that takes care of communication between NaCl and archive volume.
 * Its job is to handle communication with the naclModule.
 * @constructor
 * @param {Object} naclModule The nacl module with which the decompressor
 *     communicates.
 * @param {string} fileSystemId The file system id of the correspondent volume.
 * @param {Blob} blob The correspondent file blob for fileSystemId.
 */
var Decompressor = function(naclModule, fileSystemId, blob) {
  /**
   * The NaCl module that will decompress archives.
   * @type {Object}
   * @private
   */
  this.naclModule_ = naclModule;

  /**
   * @type {string}
   * @private
   */
  this.fileSystemId_ = fileSystemId;

  /**
   * @type {Blob}
   * @private
   */
  this.blob_ = blob;

  /**
   * Requests in progress. No need to save them onSuspend for now as metadata
   * reads are restarted from start.
   * @type {Object.<number, Object>}
   */
  this.requestsInProgress = {};
};

/**
 * @return {boolean} True if there is any request in progress.
 */
Decompressor.prototype.hasRequestsInProgress = function() {
  return Object.keys(this.requestsInProgress).length > 0;
};

/**
 * Template for requests in progress. Some requests might require extra
 * information, but the callbacks onSuccess and onError are mandatory.
 * @private
 * @param {number} requestId The request id, which should be unique per every
 *     volume.
 * @param {function} onSuccess Callback to execute on success.
 * @param {function} onError Callback to execute on error.
 * @return {Object} An object with data about the request in progress.
 */
Decompressor.prototype.newRequest_ = function(requestId, onSuccess, onError) {
  console.assert(!this.requestsInProgress[requestId],
                 'There is already a request with the id ' + requestId + '.');

  this.requestsInProgress[requestId] = {
    onSuccess: onSuccess,
    onError: onError
  };
  return this.requestsInProgress[requestId];
};

/**
 * Creates a request for reading metadata.
 * @param {number} requestId The request id, which should be unique per every
 *     volume.
 * @param {function(Object.<string, Object>)} onSuccess Callback to execute once
 *     the metadata is obtained from NaCl. It has one parameter, which is the
 *     metadata itself. The metadata has as key the full path to an entry and as
 *     value information about the entry.
 * @param {function} onError Callback to execute in case of any error.
 */
Decompressor.prototype.readMetadata = function(requestId, onSuccess, onError) {
  this.newRequest_(requestId, onSuccess, onError);
  this.naclModule_.postMessage(request.createReadMetadataRequest(
      this.fileSystemId_, requestId, this.blob_.size));
};

/**
 * Processes messages from NaCl module.
 * @param {Object} data The data contained in the message from NaCl. Its
 *     types depend on the operation of the request.
 * @param {request.Operation} operation An operation from request.js.
 * @param {number} requestId The request id, which should be unique per every
 *     volume.
 */
Decompressor.prototype.processMessage = function(data, operation, requestId) {
  // Create a request reference for asynchronous calls as sometimes we delete
  // some requestsInProgress from this.requestsInProgress.
  var requestInProgress = this.requestsInProgress[requestId];
  console.assert(requestInProgress, 'No request for: ' + requestId + '.');

  switch (operation) {
    case request.Operation.READ_METADATA_DONE:
      var metadata = data[request.Key.METADATA];
      console.assert(metadata, 'No metadata.');
      requestInProgress.onSuccess(metadata);
      break;

    case request.Operation.READ_CHUNK:
      this.readChunk_(data, requestId);
      // this.requestsInProgress_[requestId] should be valid as long as NaCL
      // can still make READ_CHUNK requests.
      return;

    case request.Operation.FILE_SYSTEM_ERROR:
      console.error('File system error for <' + this.fileSystemId_ +
                    '>: ' + data[request.Key.ERROR] + '.');
      requestInProgress.onError('FAILED');
      break;

    default:
      console.error('Invalid NaCl operation: ' + operation + '.');
      requestInProgress.onError('FAILED');
  }
  delete this.requestsInProgress[requestId];
};

/**
 * Reads a chunk of data from this.blob_ for READ_CHUNK operation.
 * @param {Object} data The data received from the NaCl module.
 * @param {number} requestId The request id, which should be unique per every
 *     volume.
 * @private
 */
Decompressor.prototype.readChunk_ = function(data, requestId) {
  var offset = data[request.Key.OFFSET];
  var length = data[request.Key.LENGTH];
  // Explicit check if offset is undefined as it can be 0.
  console.assert(offset !== undefined && offset >= 0 &&
                 offset < this.blob_.size, 'Invalid offset');
  console.assert(length && length > 0, 'Invalid length');

  offset = Number(offset);  // Received as string. See request.js.
  length = Math.min(this.blob_.size - offset, length);

  // Read a chunk from offset to offset + length.
  var blob = this.blob_.slice(offset, offset + length);
  var fileReader = new FileReader();
  var decompressor = this;  // Workaround for gjslint, which gives warning for
                            // function() { ... }.bind(this);

  fileReader.onload = function(event) {
    decompressor.naclModule_.postMessage(request.createReadChunkDoneResponse(
        decompressor.fileSystemId_, requestId, event.target.result));
  };

  fileReader.onerror = function(event) {
    console.error('Failed to read a chunk of data from the archive.');
    decompressor.naclModule_.postMessage(request.createReadChunkErrorResponse(
        decompressor.fileSystemId_, requestId));
  };

  fileReader.readAsArrayBuffer(blob);
};

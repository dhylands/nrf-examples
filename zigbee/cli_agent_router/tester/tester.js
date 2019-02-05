#!/usr/bin/env node

'use strict';

const assert = require('assert');
const commandLineArgs = require('command-line-args');
const deconzApi = require('deconz-api');
const EventEmitter = require('events');
const SerialPort = require('serialport');
const util = require('util');
const zdo = require('zigbee-zdo');

const C = deconzApi.constants;

let DEBUG_flow = false;
let DEBUG_frames = true;
let DEBUG_frameDetail = false;
let DEBUG_frameParsing = false;
let DEBUG_rawFrames = false;
let DEBUG_slip = false;

const WAIT_TIMEOUT_DELAY = 1 * 1000;
// const EXTENDED_TIMEOUT_DELAY = 10 * 1000;
const WAIT_RETRY_MAX = 3;   // includes initial send

const PARAM = [
  C.PARAM_ID.MAC_ADDRESS,
  // C.PARAM_ID.NETWORK_PANID16,
  // C.PARAM_ID.NETWORK_ADDR16,
  C.PARAM_ID.NETWORK_PANID64,
  // C.PARAM_ID.APS_DESIGNATED_COORDINATOR,
  C.PARAM_ID.SCAN_CHANNELS,
  // C.PARAM_ID.APS_PANID64,
  // C.PARAM_ID.TRUST_CENTER_ADDR64,
  // C.PARAM_ID.SECURITY_MODE,
  // C.PARAM_ID.NETWORK_KEY,
  C.PARAM_ID.OPERATING_CHANNEL,
  // C.PARAM_ID.PROTOCOL_VERION,
  // C.PARAM_ID.NETWORK_UPDATE_ID,
];

class WatchDogTimer extends EventEmitter {
  constructor(timeout) {
    super();
    this.timeout = timeout;
    this.kick();
  }

  kick() {
    if (this.timer) {
      clearTimeout(this.timer);
    }
    this.timer = setTimeout(() => {
      this.trip();
    }, this.timeout);
  }

  trip() {
    this.emit('timeout', this);
  }
}

const SEND_FRAME = 0x01;
const WAIT_FRAME = 0x02;
const EXEC_FUNC = 0x03;
const RESOLVE_SET_PROPERTY = 0x04;
const MIN_COMMAND_TYPE = 0x01;
const MAX_COMMAND_TYPE = 0x04;

class Command {
  constructor(cmdType, cmdData, priority) {
    this.cmdType = cmdType;
    this.cmdData = cmdData;
    this.priority = priority;
  }

  print(adapter, idx) {
    let prioStr = 'p:-';
    if (typeof this.priority !== 'undefined') {
      prioStr = `p:${this.priority}`;
    }

    const idxStr = `| ${`    ${idx}`.slice(-4)}: ${prioStr} `;
    switch (this.cmdType) {
      case SEND_FRAME: {
        adapter.dumpFrame(`${idxStr}SEND:`, this.cmdData, false);
        break;
      }
      case WAIT_FRAME:
        console.log(`${idxStr}WAIT`);
        break;
      case EXEC_FUNC:
        console.log(`${idxStr}EXEC:`, this.cmdData[1].name);
        break;
      case RESOLVE_SET_PROPERTY:
        console.log(`${idxStr}RESOLVE_SET_PROPERTY`);
        break;
      default:
        console.log(`${idxStr}UNKNOWN: ${this.cmdType}`);
    }
  }
}

function FUNC(ths, func, args) {
  return new Command(EXEC_FUNC, [ths, func, args]);
}

function serialWriteError(error) {
  if (error) {
    console.log('SerialPort.write error:', error);
    throw error;
  }
}

class DeconzTest {
  constructor(port) {
    this.port = port;

    this.paramIdx = 0;

    this.running = false;
    this.cmdQueue = [];
    this.frameDumped = false;

    this.dc = new deconzApi.DeconzAPI({raw_frames: DEBUG_rawFrames});
    this.zdo = this.dc.zdo;

    this.dc.on('error', (err) => {
      console.error('deConz error:', err);
    });

    if (DEBUG_rawFrames) {
      this.dc.on('frame_raw', (rawFrame) => {
        console.log('Rcvd:', rawFrame);
        if (this.dc.canParse(rawFrame)) {
          try {
            const frame = this.dc.parseFrame(rawFrame);
            try {
              this.handleFrame(frame);
            } catch (e) {
              console.error('Error handling frame_raw');
              console.error(e);
              console.error(util.inspect(frame, {depth: null}));
            }
          } catch (e) {
            console.error('Error parsing frame_raw');
            console.error(e);
            console.error(rawFrame);
          }
        }
      });
    } else {
      this.dc.on('frame_object', (frame) => {
        try {
          this.handleFrame(frame);
        } catch (e) {
          console.error('Error handling frame_object');
          console.error(e);
          console.error(util.inspect(frame, {depth: null}));
        }
      });
    }

    this.serialport = new SerialPort(port.comName, {
      baudRate: 38400,
    }, (err) => {
      if (err) {
        console.log('SerialPort open err =', err);
        return;
      }

      this.serialport.on('data', (chunk) => {
        if (DEBUG_slip) {
          console.log('Rcvd Chunk:', chunk);
        }
        this.dc.parseRaw(chunk);
      });

      this.wdt = new WatchDogTimer(5000);
      this.wdt.on('timeout', () => {
        console.log('Closing serial port');
        this.serialport.close();
      });
      this.queueCommands([
        FUNC(this, this.readParameters),
        // FUNC(this, this.version),
        FUNC(this, this.dumpParameters),
        // FUNC(this, this.configureIfNeeded),
        // FUNC(this, this.scan),
        //
        // FUNC(this, this.readParameters),
        // FUNC(this, this.dumpParameters),
      ]);
    });
  }

  configureIfNeeded() {
    if (DEBUG_flow) {
      console.log('configureIfNeeded');
    }
  }

  dumpCommands(commands) {
    if (typeof commands === 'undefined') {
      commands = this.cmdQueue;
    }
    console.log(`Commands (${commands.length})`);
    for (const idx in commands) {
      const cmd = commands[idx];
      cmd.print(this, idx);
    }
    console.log('---');
  }

  dumpFrame(label, frame, dumpFrameDetail) {
    if (typeof dumpFrameDetail === 'undefined') {
      dumpFrameDetail = DEBUG_frameDetail;
    }
    this.frameDumped = true;
    this.dc.dumpFrame(label, frame, dumpFrameDetail);
  }

  dumpParameters() {
    for (const paramId of PARAM) {
      const param = C.PARAM_ID[paramId];
      const label = `                    ${param.label}`.slice(-20);
      let value = this[param.fieldName];
      if (paramId == C.PARAM_ID.SCAN_CHANNELS) {
        value = `00000000${value.toString(16)}`.slice(-8);
      }
      console.log(`${label}: ${value}`);
    }
    // console.log(`             Version: ${this.version}`);
  }

  handleExplcitRx(frame) {
    console.log('handleExplicitRx');
    if (this.zdo.isZdoFrame(frame)) {
      try {
        this.zdo.parseZdoFrame(frame);
        if (DEBUG_frames) {
          this.dumpFrame('Rcvd:', frame);
        }
        const clusterId = parseInt(frame.clusterId, 16);
        if (clusterId in DeconzTest.zdoClusterHandler) {
          DeconzTest.zdoClusterHandler[clusterId].call(this, frame);
        } else {
          console.log('No handler for ZDO cluster:',
                      zdo.getClusterIdAsString(clusterId));
        }
      } catch (e) {
        console.error('handleExplicitRx:',
                      'Caught an exception parsing ZDO frame');
        console.error(e);
        console.error(util.inspect(frame, {depth: null}));
      }
    }
  }

  handleFrame(frame) {
    frame.received = true;
    if (DEBUG_frameParsing) {
      this.dumpFrame('Rcvd (before parsing):', frame);
    }
    this.frameDumped = false;
    const frameHandler = DeconzTest.frameHandler[frame.type];
    if (frameHandler) {
      if (this.waitFrame && this.waitFrame.extraParams) {
        frame.extraParams = this.waitFrame.extraParams;
      }
      frameHandler.call(this, frame);
    }
    if (DEBUG_frames && !this.frameDumped) {
      this.dumpFrame('Rcvd:', frame);
    }

    if (frame.type == C.FRAME_TYPE.APS_DATA_INDICATION ||
        frame.type == C.FRAME_TYPE.APS_DATA_CONFIRM) {
      this.deviceStateUpdateInProgress = false;
    }

    if (this.waitFrame) {
      if (DEBUG_flow) {
        console.log('Waiting for', this.waitFrame);
      }
      let match = true;
      const specialNames = [
        'sendOnSuccess',
        'callback',
        'timeoutFunc',
        'waitRetryCount',
        'waitRetryMax',
        'extraParams',
      ];
      for (const propertyName in this.waitFrame) {
        if (specialNames.includes(propertyName)) {
          continue;
        }
        if (this.waitFrame[propertyName] != frame[propertyName]) {
          match = false;
          break;
        }
      }
      if (match) {
        if (DEBUG_flow || DEBUG_frameDetail) {
          console.log('Wait satisified');
        }
        // const sendOnSuccess = this.waitFrame.sendOnSuccess;
        const callback = this.waitFrame.callback;
        this.waitFrame = null;
        if (this.waitTimeout) {
          clearTimeout(this.waitTimeout);
          this.waitTimeout = null;
        }
        // if (sendOnSuccess && frame.status === 0) {
        //   this.sendFrames(sendOnSuccess);
        // }
        if (callback) {
          callback(frame);
        }
      } else if (DEBUG_flow || DEBUG_frameDetail) {
        console.log('Wait NOT satisified');
        console.log('    waitFrame =', this.waitFrame);
      }
    }

    if (!this.deviceStateUpdateInProgress) {
      if (frame.hasOwnProperty('dataIndication') && frame.dataIndication) {
        // There's a frame ready to be read.
        this.deviceStateUpdateInProgress = true;
        this.queueCommandsAtFront([
          new Command(SEND_FRAME, {
            type: C.FRAME_TYPE.APS_DATA_INDICATION,
          }),
          new Command(WAIT_FRAME, {
            type: C.FRAME_TYPE.APS_DATA_INDICATION,
          })]);
      } else if (frame.hasOwnProperty('dataConfirm') && frame.dataConfirm) {
        // There's a send confirmation ready to be read
        this.deviceStateUpdateInProgress = true;
        this.queueCommandsAtFront([
          new Command(SEND_FRAME, {
            type: C.FRAME_TYPE.APS_DATA_CONFIRM,
          }),
          new Command(WAIT_FRAME, {
            type: C.FRAME_TYPE.APS_DATA_CONFIRM,
          })]);
      }
    }
    this.run();
  }

  flattenCommands(cmdSeq) {
    const cmds = [];
    for (const cmd of cmdSeq) {
      if (cmd.constructor === Array) {
        for (const cmd2 of cmd) {
          assert(cmd2 instanceof Command,
                 '### Expecting instance of Command ###');
          assert(typeof cmd2.cmdType === 'number',
                 `### Invalid Command Type: ${cmd2.cmdType} ###`);
          assert(cmd2.cmdType >= MIN_COMMAND_TYPE,
                 `### Invalid Command Type: ${cmd2.cmdType} ###`);
          assert(cmd2.cmdType <= MAX_COMMAND_TYPE,
                 `### Invalid Command Type: ${cmd2.cmdType} ###`);
          cmds.push(cmd2);
        }
      } else {
        assert(cmd instanceof Command,
               '### Expecting instance of Command ###');
        assert(typeof cmd.cmdType === 'number',
               `### Invalid Command Type: ${cmd.cmdType} ###`);
        assert(cmd.cmdType >= MIN_COMMAND_TYPE,
               `### Invalid Command Type: ${cmd.cmdType} ###`);
        assert(cmd.cmdType <= MAX_COMMAND_TYPE,
               `### Invalid Command Type: ${cmd.cmdType} ###`);
        cmds.push(cmd);
      }
    }
    // Now that we've flattened things, make sure all of the commands
    // have the same priority.
    const priority = cmds[0].priority;
    for (const cmd of cmds) {
      cmd.priority = priority;
    }
    return cmds;
  }

  getManagementLqi(node, startIndex) {
    if (!startIndex) {
      startIndex = 0;
    }
    if (DEBUG_flow) {
      console.log('getManagementLqi node.addr64 =', node.addr64,
                  'startIndex:', startIndex);
    }

    this.queueCommandsAtFront(this.getManagementLqiCommands(node, startIndex));
  }

  getManagementLqiCommands(node, startIndex) {
    if (!startIndex) {
      startIndex = 0;
    }
    if (DEBUG_flow) {
      console.log('getManagementLqiCommands node.addr64 =', node.addr64,
                  'startIndex:', startIndex);
    }
    const lqiFrame = this.zdo.makeFrame({
      destination64: node.addr64,
      destination16: node.addr16,
      clusterId: zdo.CLUSTER_ID.MANAGEMENT_LQI_REQUEST,
      startIndex: startIndex,
    });
    return [
      new Command(SEND_FRAME, lqiFrame),
      // new Command(WAIT_FRAME, {
      //   type: C.FRAME_TYPE.ZIGBEE_TRANSMIT_STATUS,
      //   id: lqiFrame.id,
      // }),
      FUNC(this, this.getManagementLqiNext, [node]),
    ];
  }

  getManagementLqiNext(node) {
    if (this.nextStartIndex >= 0) {
      const nextStartIndex = this.nextStartIndex;
      this.nextStartIndex = -1;
      this.queueCommandsAtFront(
        this.getManagementLqiCommands(node, nextStartIndex));
    }
  }

  handleManagementLqiResponse(frame) {
    if (DEBUG_flow) {
      console.log('Processing CLUSTER_ID.MANAGEMENT_LQI_RESPONSE');
    }
    // const node = this.createNodeIfRequired(frame.remote64, frame.remote16);

    for (let i = 0; i < frame.numEntriesThisResponse; i++) {
      const neighbor = frame.neighbors[i];
      // const neighborIndex = frame.startIndex + i;
      // node.neighbors[neighborIndex] = neighbor;
      if (DEBUG_flow) {
        console.log('Added neighbor', neighbor.addr64);
      }
      /*
      const neighborNode =
        this.createNodeIfRequired(neighbor.addr64, neighbor.addr16);
      if (neighborNode) {
        neighborNode.deviceType = neighbor.deviceType;
        neighborNode.rxOnWhenIdle = neighbor.rxOnWhenIdle;
      }
      */
    }

    if (frame.startIndex + frame.numEntriesThisResponse <
        frame.numEntries) {
      this.nextStartIndex =
        frame.startIndex + frame.numEntriesThisResponse;
    } else {
      this.nextStartIndex = -1;
    }
  }

  makeFrameWaitFrame(sendFrame, waitFrame, priority) {
    return [
      new Command(SEND_FRAME, sendFrame, priority),
      new Command(WAIT_FRAME, waitFrame, priority),
    ];
  }

  readParameter() {
    if (this.paramIdx >= PARAM.length) {
      return;
    }
    const paramId = PARAM[this.paramIdx];
    const readParamFrame = {
      type: C.FRAME_TYPE.READ_PARAMETER,
      paramId: paramId,
    };
    this.queueCommandsAtFront([
      new Command(SEND_FRAME, readParamFrame),
      new Command(WAIT_FRAME, {
        type: C.FRAME_TYPE.READ_PARAMETER,
        paramId: paramId,
        callback: (frame) => {
          if (this.paramIdx < PARAM.length) {
            const paramId = PARAM[this.paramIdx];
            const fieldName = C.PARAM_ID[paramId].fieldName;
            this[fieldName] = frame[fieldName];
            this.paramIdx++;
            this.readParameter(this.paramIdx);
          }
        },
      }),
    ]);
  }

  readParameters() {
    this.paramIdx = 0;
    this.readParameter();
  }

  scan() {
    this.getManagementLqi({
      addr16: '0000',
      addr64: this.macAddress,
    });
  }

  sendFrameNow(frame) {
    if (DEBUG_flow) {
      console.log('sendFrameNow');
    }
    if (DEBUG_frames) {
      this.dumpFrame('Sent:', frame);
    }
    const rawFrame = this.dc.buildFrame(frame);
    if (DEBUG_rawFrames) {
      console.log('Sent:', rawFrame);
    }
    this.serialport.write(rawFrame, serialWriteError);
  }

  writeParameter(frame) {
    frame.type = C.FRAME_TYPE.WRITE_PARAMETER;
    this.queueCommandsAtFront([
      new Command(SEND_FRAME, frame),
      new Command(WAIT_FRAME, {
        type: C.FRAME_TYPE.WRITE_PARAMETER,
        paramId: frame.paramId,
      }),
    ]);
  }

  queueCommands(cmdSeq) {
    if (DEBUG_flow) {
      console.log('queueCommands');
    }
    cmdSeq = this.flattenCommands(cmdSeq);
    const priority = cmdSeq[0].priority;
    let idx = -1;
    if (typeof priority !== 'undefined') {
      // The command being inserted has a priority. This means
      // it will get inserted in front of the first command in
      // the queue with no priority, or a command with a priority
      // greater than the one being inserted.
      idx = this.cmdQueue.findIndex((cmd) => {
        return typeof cmd.priority === 'undefined' ||
               priority < cmd.priority;
      });
    }
    if (idx < 0) {
      idx = this.cmdQueue.length;
    }
    this.cmdQueue.splice(idx, 0, ...cmdSeq);
    if (DEBUG_flow) {
      this.dumpCommands();
    }
    if (!this.running) {
      this.run();
    }
  }

  queueCommandsAtFront(cmdSeq) {
    if (DEBUG_flow) {
      console.log('queueCommandsAtFront');
    }
    cmdSeq = this.flattenCommands(cmdSeq);
    const priority = cmdSeq[0].priority;
    let idx = -1;
    if (typeof priority === 'undefined') {
      // The command being inserted has no priority. This means
      // it will be inserted in front of the first command
      // with no priority.
      idx = this.cmdQueue.findIndex((cmd) => {
        return typeof cmd.priority === 'undefined';
      });
    } else {
      // The command being inserted has a priority. This means
      // it will get inserted in front of the first command in
      // the queue with no priority, or a command with a priority
      // greater than or equal to the one being inserted.
      idx = this.cmdQueue.findIndex((cmd) => {
        return typeof cmd.priority === 'undefined' ||
               priority <= cmd.priority;
      });
    }
    if (idx < 0) {
      idx = this.cmdQueue.length;
    }
    this.cmdQueue.splice(idx, 0, ...cmdSeq);
    if (DEBUG_flow) {
      this.dumpCommands();
    }
    if (!this.running) {
      this.run();
    }
  }

  run() {
    if (DEBUG_flow) {
      console.log('run queue len =', this.cmdQueue.length,
                  'running =', this.running);
    }
    if (this.waitFrame) {
      if (DEBUG_flow) {
        console.log('Queue stalled waiting for frame.');
      }
      return;
    }
    if (this.running) {
      return;
    }
    this.running = true;
    while (this.cmdQueue.length > 0 && !this.waitFrame) {
      const cmd = this.cmdQueue.shift();
      switch (cmd.cmdType) {
        case SEND_FRAME: {
          const frame = cmd.cmdData;
          let sentPrefix = '';
          if (frame.resend) {
            sentPrefix = 'Re';
          }
          if (DEBUG_flow) {
            console.log(`${sentPrefix}SEND_FRAME`);
          }
          if (DEBUG_frames) {
            this.dumpFrame(`${sentPrefix}Sent:`, frame);
          }
          // The xbee library returns source and destination endpoints
          // as a 2 character hex string. However, the frame builder
          // expects a number. And since we use the endpoint as a key
          // in the node.activeEndpoints, we get the endpoint as a string
          // containing a decimal number. So we put these asserts in to
          // make sure that we're dealing with numbers and not strings.
          if (frame.hasOwnProperty('sourceEndpoint') &&
              typeof frame.sourceEndpoint !== 'number') {
            console.log(frame);
            assert(typeof frame.sourceEndpoint === 'number',
                   'Expecting sourceEndpoint to be a number');
          }
          if (frame.hasOwnProperty('destinationEndpoint') &&
              typeof frame.destinationEndpoint !== 'number') {
            console.log(frame);
            assert(typeof frame.destinationEndpoint === 'number',
                   'Expecting destinationEndpoint to be a number');
          }
          const rawFrame = this.dc.buildFrame(frame);
          if (DEBUG_rawFrames) {
            console.log(`${sentPrefix}Sent:`, rawFrame);
          }
          this.serialport.write(rawFrame, serialWriteError);
          this.lastFrameSent = frame;
          this.wdt.kick();
          break;
        }
        case WAIT_FRAME: {
          this.waitFrame = cmd.cmdData;
          if (!this.waitFrame.hasOwnProperty('waitRetryCount')) {
            this.waitFrame.waitRetryCount = 1;
          }
          if (!this.waitFrame.hasOwnProperty('waitRetryMax')) {
            this.waitFrame.waitRetryMax = WAIT_RETRY_MAX;
          }
          const timeoutDelay = WAIT_TIMEOUT_DELAY;
          /*
          if (this.lastFrameSent && this.lastFrameSent.destination64) {
            const node = this.nodes[this.lastFrameSent.destination64];
            if (node && node.extendedTimeout) {
              timeoutDelay = EXTENDED_TIMEOUT_DELAY;
            }
          } */
          if (DEBUG_frameDetail) {
            console.log('WAIT_FRAME type:', this.waitFrame.type,
                        'timeoutDelay =', timeoutDelay);
          }
          this.waitTimeout = setTimeout(this.waitTimedOut.bind(this),
                                        timeoutDelay);
          break;
        }
        case EXEC_FUNC: {
          const ths = cmd.cmdData[0];
          const func = cmd.cmdData[1];
          const args = cmd.cmdData[2];
          if (DEBUG_frameDetail) {
            console.log('EXEC_FUNC', func.name);
          }
          func.apply(ths, args);
          break;
        }
        case RESOLVE_SET_PROPERTY: {
          const property = cmd.cmdData;
          if (DEBUG_frameDetail) {
            console.log('RESOLVE_SET_PROPERTY', property.name);
          }
          const deferredSet = property.deferredSet;
          if (deferredSet) {
            property.deferredSet = null;
            deferredSet.resolve(property.value);
          }
          break;
        }
        default:
          console.log('#####');
          console.log(`##### UNKNOWN COMMAND: ${cmd.cmdType} #####`);
          console.log('#####');
          break;
      }
    }
    this.running = false;
  }

  sendFrameWaitFrameAtFront(sendFrame, waitFrame, priority) {
    this.queueCommandsAtFront(
      this.makeFrameWaitFrame(sendFrame, waitFrame, priority));
  }

  version() {
    const versionFrame = {
      type: C.FRAME_TYPE.VERSION,
    };
    this.queueCommandsAtFront([
      new Command(SEND_FRAME, versionFrame),
      new Command(WAIT_FRAME, {
        type: C.FRAME_TYPE.VERSION,
        callback: (frame) => {
          this.version = frame.version;
        },
      }),
    ]);
  }

  waitTimedOut() {
    if (DEBUG_frameDetail) {
      console.log('WAIT_FRAME timed out');
    }
    // We timed out waiting for a response, resend the last command.
    clearTimeout(this.waitTimeout);
    this.waitTimeout = null;

    // We need to set waitFrame back to null in order for the run
    // function to do anything.
    const waitFrame = this.waitFrame;
    const timeoutFunc = waitFrame.timeoutFunc;
    this.waitFrame = null;

    if (waitFrame.waitRetryCount >= waitFrame.waitRetryMax) {
      if (DEBUG_flow) {
        console.log('WAIT_FRAME exceeded max retry count');
      }
      if (timeoutFunc) {
        timeoutFunc();
      }
      // We've tried a few times, but no response.
      if (this.waitFrameTimeoutFunc) {
        this.waitFrameTimeoutFunc(this.lastFrameSent);
      }
      if (!this.running) {
        this.run();
      }
      return;
    }

    if (this.lastFrameSent && waitFrame) {
      waitFrame.waitRetryCount += 1;
      if (DEBUG_frames) {
        console.log('Resending',
                    `(${waitFrame.waitRetryCount}/${waitFrame.waitRetryMax})`,
                    '...');
      }
      this.lastFrameSent.resend = true;

      // Uncomment the following to cause the new send to go out with
      // a new ID. In the cases I've seen, sending with the new or
      // existing ID doesn't change the behaviour. Leaving the ID the
      // same allows up to pick up a late response from an earlier
      // request.

      // this.lastFrameSent.id = this.nextFrameId();
      // if (waitFrame.id) {
      //   waitFrame.id = this.lastFrameSent.id;
      // }

      this.sendFrameWaitFrameAtFront(this.lastFrameSent, waitFrame);
    }
  }
}

const fh = DeconzTest.frameHandler = {};
fh[C.FRAME_TYPE.APS_DATA_INDICATION] =
  DeconzTest.prototype.handleExplicitRx;

const zch = DeconzTest.zdoClusterHandler = {};
zch[zdo.CLUSTER_ID.MANAGEMENT_LQI_RESPONSE] =
  DeconzTest.prototype.handleManagementLqiResponse;

function isMozIotnrfDongle(port) {
  return (port.vendorId === '1915' &&
          port.productId === '520f' &&
          port.pnpId.includes('MozIoT') &&
          port.pnpId.includes('if00'));
}

const optionsDefs = [
  {name: 'detail', alias: 'd', type: Boolean},
  {name: 'flow', alias: 'w', type: Boolean},
  {name: 'frames', alias: 'f', type: Boolean},
  {name: 'parsing', alias: 'p', type: Boolean},
  {name: 'raw', alias: 'r', type: Boolean},
  {name: 'slip', alias: 's', type: Boolean},
];
const options = commandLineArgs(optionsDefs);
DEBUG_flow = options.flow;
DEBUG_frames = options.frames;
DEBUG_frameParsing = options.parsing;
DEBUG_rawFrames = options.raw;
DEBUG_frameDetail = options.detail;
DEBUG_slip = options.slip;

SerialPort.list((error, ports) => {
  if (error) {
    console.error(error);
    return;
  }

  // console.log(ports);

  const nrfPorts = ports.filter(isMozIotnrfDongle);
  if (nrfPorts.length == 0) {
    console.error('No MozIot nrf dongles found');
    return;
  }
  if (nrfPorts.length > 1) {
    console.error('Too many MozIot nrf dongles found');
    return;
  }
  const portName = nrfPorts[0].comName;
  console.log('Found MozIot nrf52840 at', portName);
  const _dcTest = new DeconzTest(nrfPorts[0]);
});

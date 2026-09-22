// code for ThingsBoard's transformation-node (extract ChirpStack object from message)
var newMsg = {};
// check msg and msg.object exist
if (msg && msg.object) {
    if (typeof msg.object === 'string') {
        try {
            newMsg = JSON.parse(msg.object);
        } catch (e) {
            newMsg = msg; // keep original if parse failed
        }
    } else {
        newMsg = msg.object;
    }
} else {
    newMsg = msg;
}
// formated to ThingsBoard transformation-node
return { msg: newMsg, metadata: metadata, msgType: msgType };
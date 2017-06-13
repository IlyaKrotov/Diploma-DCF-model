enum FrameKinds {
    KIND_DATA = 100;
    KIND_ACK = 101;
}
packet Frame {
    simtime_t frameDuration;
}
packet DataFrame extends Frame {
    kind = KIND_DATA;
}
packet AckFrame extends Frame {
    kind = KIND_ACK;
}
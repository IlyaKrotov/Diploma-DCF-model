simple Generator
{
    parameters:
        volatile int pkLenBits @unit(b) = default(exponential(200b));  // packet length in bits
        volatile double iaTime @unit(s) = default(exponential(1s));  // packet interarrival time
        double txRate @unit(bps);         // transmission rate
        @display("i=device/pc_s");

    gates:
       	output out;
}

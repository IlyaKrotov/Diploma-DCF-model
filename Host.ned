simple Host
{
    parameters:
    	int numHosts;
        double txRate @unit(bps);         // transmission rate
        volatile double radioDelay @unit(s) = default(exponential(1ms));       // propagation delay of radio link
        volatile int pkLenBits @unit(b) = default(exponential(200b));  // packet length in bits
        volatile double iaTime @unit(s) = default(exponential(1s));  // packet interarrival time
        double slotTime @unit(s);         // zero means no slots (pure Aloha)
        @display("i=device/pc_s");

        double DIFS @unit(s);
        double SIFS @unit(s);
        int CWmin;
        int CWmax;

        //channel is busy
        @signal[channelIsBusy](type=signalMessage);

        //stats
        @signal[send](type="double");
        @signal[waitBegin](type="double");  // increases with each new frame arriving to the server and drops to 0 if the channel becomes finally idle
        @signal[receive](type="double");  // for successful receptions (non-collisions): 1 at the start of the reception, 0 at the end of the reception
        @signal[ACKWaitLength](type="double");  // the length of the last collision period at the end of the collision period
  
        @statistic[waitBegin](source="waitBegin"; record=vector?, histogram; title="wait length");
        @statistic[ACKWaitLength](source="ACKWaitLength"; record=vector?,histogram,mean; title="ACKWait length");
        @statistic[send](source="send"; record=vector?,histogram,mean; title="send length");
        @statistic[receive](source="receive"; record=vector?,histogram,mean; title="recieve length");

    gates:
       	input in @directIn;
}
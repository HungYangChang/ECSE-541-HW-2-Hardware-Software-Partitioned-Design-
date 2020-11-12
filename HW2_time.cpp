#include "systemc.h"
#include <stdio.h> 
#include <queue> 
#include <string>
#include <iomanip>

#define LOOPS 1000

// memory size parameter
#define SIZE 5
#define MEM_length_row_column (SIZE+1)
#define MEM_SIZE 108
#define MEM_a_ADDR 0
#define MEM_b_ADDR MEM_a_ADDR + MEM_abc_length
#define MEM_c_ADDR MEM_b_ADDR + MEM_abc_length 
#define MEM_abc_length 36
#define clock_period 6.66


#define ADDR_Calling_hardware 2000



// mode parameter
#define Mode_W 0
#define Mode_R 1
#define Mode_Cal 2
#define MODE_None 3

int cycle = 0;

//interface
class bus_master_if : virtual public sc_interface
{
  public:     
    virtual void Request(unsigned mst_id, unsigned addr, unsigned op, unsigned len) = 0;     
    virtual bool WaitForAcknowledge(unsigned mst_id) = 0;     
    virtual void ReadData(unsigned &data) = 0;     
    virtual void WriteData(unsigned data) = 0; 
};


// Bus Servant Interface
class bus_minion_if : virtual public sc_interface
{
  public:     
    virtual void Listen(unsigned &req_addr, unsigned &req_op, unsigned &req_len) = 0;     
    virtual void Acknowledge() = 0;     
    virtual void SendReadData(unsigned data) = 0;     
    virtual void ReceiveWriteData(unsigned &data) = 0; 
};

// 150MHz
// toscillator
class OSCILLATOR: public sc_module
{
public :
  sc_out<sc_logic> Clk_out;

  SC_HAS_PROCESS(OSCILLATOR);
  void oscillator()
  {
    while(true) 
    {
      Clk_out.write(SC_LOGIC_0);
      wait(clock_period/2, SC_NS);
      Clk_out.write(SC_LOGIC_1);
      wait(clock_period/2, SC_NS);
    }
  }
  // constructor 
  OSCILLATOR(sc_module_name name) : sc_module(name)
  { 
    //cout << " @" << sc_time_stamp() << " OSCILLATOR Thread starts" << endl;
    SC_THREAD(oscillator);
  }
};


struct data_info
{
  unsigned master_id;
  unsigned address;
  unsigned op;
  unsigned len;
};

// bus with bus_master_if and bus_minion_if
// behave like hw1 memory
// tbus Done
class Bus: public sc_module, public bus_master_if, public bus_minion_if
{
  public: 
    // in master 
    data_info masterbuffer;
    data_info current;

    // initialization
    unsigned length_sent =0;
    unsigned length_recieved =0;

    // Impelment bus arbiter
    std::queue<unsigned int> FIFO_bus;
    bool acknowledged = false;
   
    sc_in<sc_logic> Clk;

    SC_HAS_PROCESS(Bus);

    //consturctor
    Bus (sc_module_name name) : sc_module(name)
    {      
      SC_THREAD(Bus_arbiter);
        sensitive << Clk.pos();

      current.master_id = 3;
      masterbuffer.master_id = current.master_id;

      current.address = 1000;
      masterbuffer.address = current.address;

      current.op = MODE_None;
      masterbuffer.op = current.op;

      current.len = 10000;
      masterbuffer.len = current.len ;
    }



  void Bus_arbiter() 
  {
    while (true) 
    { 
      wait(Clk.posedge_event());
      // if  (current.op != MODE_None && current.len >= 0)
      if (current.len >= 0) 
      {
          current.master_id = masterbuffer.master_id;
          current.address = masterbuffer.address;
          current.op = masterbuffer.op;
          current.len = masterbuffer.len;
          length_sent = 0;
          length_recieved = 0;

          // print out mode other than MODE_None
          if (current.op != MODE_None)
          {
            cout << " @" << sc_time_stamp() << " Master-Minion connected now... Master id: " << current.master_id  
            << " (0SW, 1HW) Address: " << current.address << " Operation mode: " << current.op << "(0W, 1R, 2C) length: " << current.len << endl;
          }
      }
    }
  }

  void Request(unsigned mst_id, unsigned addr, unsigned op, unsigned len) 
  {
    // 2 cycle
    wait(Clk.posedge_event());
    wait(Clk.posedge_event());

    if (mst_id >= 0) 
    {
      masterbuffer.master_id = mst_id;
      masterbuffer.address = addr;
      masterbuffer.op = op;
      masterbuffer.len = len;
      cout << " @" << sc_time_stamp() << " Master rquest now.... Master id: " << mst_id << "(0SW, 1HW), Address: "
                  << addr << ", Op: " << op << "(0W, 1R, 2C) , Length: " << len << "\n";
    }
  }

  bool WaitForAcknowledge(unsigned mst_id) 
  {
    while(current.master_id != mst_id || !acknowledged) 
      wait(Clk.posedge_event());
    acknowledged = false;
    return true;
    // master id  = mst_id or Ack = ture
  }

  void ReadData(unsigned &data) 
  {
    // 2 cycles
    wait(Clk.posedge_event());
    wait(Clk.posedge_event());
       
    if (length_recieved < current.len)
    {
      // empty then wait...
      while (FIFO_bus.empty()) 
        wait(Clk.posedge_event());

      // bus FIFO
      // (pop)front oldest   newest end(push)
      
      data = FIFO_bus.front();
      FIFO_bus.pop();
      length_recieved++;
    } 
  }


  void WriteData(unsigned data) 
  {
    // 1 cycle
    wait(Clk.posedge_event());

    if (length_sent < current.len) 
    {
      length_sent++;
      FIFO_bus.push(data);
      // cout << " @" << sc_time_stamp() << "WriteData"<< endl;
    } 
  }

  // ============================================minion============================
  // ============================================minion============================
  // ============================================minion============================

  void Listen(unsigned &req_addr, unsigned &req_op, unsigned&req_len) 
  {
      // cout << " @" << sc_time_stamp() << " listening"  << req_addr << req_op << req_len << endl;
      req_op = current.op;
      req_len = current.len;
      if (req_len >= 0 && length_recieved == 0 && length_sent == 0) 
      {
        req_addr = current.address;
        // cout << " @@" << endl; 
      }  
      else 
      {
        // set to default
        req_addr = 1000;
        // cout << " @" << endl;
      }
  }

  void Acknowledge() 
  {
    // 1 cycle
    wait(Clk.posedge_event());

    while (acknowledged == true)
      wait(Clk.posedge_event());
    acknowledged = true;

    //reset operation (only access once)
    masterbuffer.op = 3;
    masterbuffer.address = 1000;
    masterbuffer.len = 1000;

  }

  void SendReadData(unsigned data) 
  {
    if (length_sent < current.len) 
    {
      length_sent++;
      wait(Clk.posedge_event());
      FIFO_bus.push(data);
    } 
  }

  void ReceiveWriteData(unsigned &data) 
  {
    if (length_recieved < current.len) 
    {
      while (FIFO_bus.empty())
        wait(Clk.posedge_event());

      length_recieved++;
      data = FIFO_bus.front();
      FIFO_bus.pop();
    } 
  }

}; 



//Memory always be minion "minion"
// tmemory Done!
class Memory: public sc_module
{
  public: 
    // bus to memory
    sc_port<bus_minion_if> Bus_to_memory;
    sc_in<sc_logic> Clk;

    // int i,j,k;
    unsigned req_addr;
    unsigned req_op;
    unsigned req_len;

    unsigned MEM[MEM_SIZE];
    int a[MEM_abc_length] = { 0,0,0,0,0,0,0,0,9,4,7,9,0,12,14,15,16,11,0,2,3,4,5,6,0,4,3,2,1,2,0,2,7,6,4,9 };
    int b[MEM_abc_length] = { 0,0,0,0,0,0,0,0,9,4,7,9,0,12,14,15,16,11,0,2,3,4,5,6,0,4,3,2,1,2,0,2,7,6,4,9 };


    SC_HAS_PROCESS(Memory);
    
    // constructor 
    Memory (sc_module_name name) : sc_module(name)
    {
      // initalize memory
      // cout << " @" << sc_time_stamp() << "  Starting storing initial value to memory....." << endl;
      for(int i =0; i<MEM_abc_length; i++)
        {
          MEM[MEM_a_ADDR+i]=a[i];
          MEM[MEM_b_ADDR+i]=b[i];
          MEM[MEM_c_ADDR+i]=0; // c[i] initalize to 0
        }

      SC_THREAD(Memory_read_write);
        sensitive << Clk.pos();
    }

    void print_result()
    {
      // print 
      cout << " @" << sc_time_stamp() << "  MEM value is" << endl; 
      for(int i =0; i<MEM_SIZE; i++)            
        if ((i+1) % 36 == 0)
          cout << (int) MEM[i] << endl;
        else
          cout << (int) MEM[i] << " ,";
    }


    // memory always minion
    void Memory_read_write()
    {
      while(true) 
      {
          wait(Clk.posedge_event());
          // listen to master
          Bus_to_memory->Listen(req_addr, req_op, req_len); //&req_addr: req_addr memory address

          // check if it's in valid address
          // if(req_addr >= 0 && req_addr t 108 && req_op!= Mode_Cal) 
          if(req_addr >= 0 && req_addr <= 107) 
          {
            Bus_to_memory->Acknowledge();
            cout << " @" << sc_time_stamp() << " Address is valid, Memory served as minion now...., operation: " << req_op << " (0W, 1R)" <<endl;
            //doing write
            if (req_op == Mode_W)
            {
              for (int i=0; i<req_len; i++)
                Bus_to_memory->ReceiveWriteData(MEM[req_addr+i]); //recieve write data c to mem
              // cout << " @" << sc_time_stamp() << " Memory recieve write data successfully!" << endl;
            }


            //doing read
            else if (req_op == Mode_R)
            {
              for (int i=0; i<req_len; i++)
                Bus_to_memory->SendReadData(MEM[req_addr+i]); //send read data a or b from mem
             // cout << " @" << sc_time_stamp() << " Memory send read data successfully!" << endl;
            }
          }
          // else if (req_addr == ADDR_Calling_hardware)
          //   cout << " @" << sc_time_stamp() << " Address is valid, Memory served as minion now...., operation: " << req_op << " (0W, 1R)" <<endl;
      }
    }

};



// tell HW what to do on a c[i][j] basis,
// directing it to work on an entire row and column at a time.
// thardware Done! : masterid: 1
class Hardware: public sc_module
{
  public: 
    //  port to bus minion
    //  port to bus master 
    sc_port<bus_minion_if> Bus_to_hardware_minion;
    sc_port<bus_master_if> Bus_to_hardware_master;
    sc_in<sc_logic> Clk;

    // int n;
    // int i,j,k;

    unsigned req_addr;
    unsigned req_op;
    unsigned req_len;
    unsigned int MEM[MEM_SIZE];
    unsigned int register_A[MEM_abc_length] = {0};
    unsigned int register_B[MEM_abc_length] = {0};
    unsigned int register_C[MEM_abc_length] = {0};

    bool Ack = false;
    bool Software_Hardware_cooperation = false; // Set it to true if software hardware cooperate

    
    SC_HAS_PROCESS(Hardware);
    // constructor
    Hardware (sc_module_name name) : sc_module(name)
    {
      for (int i = 0; i < MEM_abc_length; i++) 
      {
        register_A[i] = 0;
        register_B[i] = 0;
        register_C[i] = 0;
      }

      // cout << " @" << sc_time_stamp() << "  Hardware Thread starts" << endl;
      // First listen from software
      SC_THREAD(Hardware_listen_to_software);
        sensitive << Clk.pos(); 

      // Then Software_Hardware_cooperation, doing calculation
      SC_THREAD(Hardware_cal);
        sensitive << Clk.pos();

    }

    // hardware serve as minion!!
    // listening to software
    void Hardware_listen_to_software()
    {
      while(true) 
      {
        wait(Clk.posedge_event());
        // listen to master software
        Bus_to_hardware_minion->Listen(req_addr, req_op, req_len); 
        if (req_addr == ADDR_Calling_hardware && req_op == Mode_Cal && req_len ==1) 
        {
          cout << " @" << sc_time_stamp() << " Hardware serves as minion and listen to software (master) now" << endl; 
          Bus_to_hardware_minion->Acknowledge(); //Acknowledge software
          cout << " @" << sc_time_stamp() << " Hardware acknowledged software now" << endl; 
          Software_Hardware_cooperation = true;
        }
        else 
          Software_Hardware_cooperation = false;
      }
    }

    // hardware serve as master!! 
    void Hardware_cal()
    {
      while(true) 
      {
        wait(Clk.posedge_event());
        if (Software_Hardware_cooperation)
        {
          cout << " @" << sc_time_stamp() << " Hardware and software are cooperating now..."
               << " reading A and B now...." << endl;

          // store a and b to register so that I can reuse it...
          HW_Read_From_Memory(MEM_a_ADDR, register_A);
          cout << " @" << sc_time_stamp() << " Reading Register A successfully" << endl;

          HW_Read_From_Memory(MEM_b_ADDR, register_B);
          cout << " @" << sc_time_stamp() << " Reading Register B successfully" << endl;


          // print out A & B value...
          // cout << " @" << sc_time_stamp() << " A value is" << endl;
          // for(int i =0; i<36; i++)            
          //     cout << (int) register_A[i] << ",";
          // cout << endl;

          // cout << " @" << sc_time_stamp() << " B value is" << endl;
          // for(int i =0; i<36; i++)            
          //     cout << (int) register_B[i] << ",";
          // cout << endl;


          // version 1:  doing multiplication  
          // cout << " @" << sc_time_stamp() << " Read A and B successfully... Hardware doing matrix multiplication.... " << endl;
          for (int i = 0; i<=SIZE; i++) //row 
          {
            for (int j = 0; j<=SIZE; j++) //column
            {
              unsigned int C_Sum = 0;
              for (int k = 0; k <=SIZE; k++)
              {
                //c[i][j] += a[i][k] * b[k][j];
                C_Sum = C_Sum + register_A[i * MEM_length_row_column + k] * register_B[k * MEM_length_row_column + j];
              }
              register_C[i * MEM_length_row_column +j] = C_Sum;
            }
          }



          cout << " @" << sc_time_stamp() << " Hardware finsihed matrix multiplication!" << endl;
          
          // start writing value back to memory
          HW_Write_To_Memory (MEM_c_ADDR, register_C);
          
          // set back to false once finished 
          Software_Hardware_cooperation = false;
          cout << " @" << sc_time_stamp() << " Hardware finsih writing back to memory!" << endl; 

          // print out result here
          cout << " @" << sc_time_stamp() << " C value is" << endl;
          for(int i =0; i<36; i++)            
              cout << (int) register_C[i] << ", ";
          cout <<endl;
        }
        // else 
        //   cout << " @" << sc_time_stamp() << " Hardware and software are not cooperating now..." << endl;
      }
    }

    void HW_Read_From_Memory(unsigned addr_read, unsigned int Register[MEM_abc_length])
    {
      cout << " @" << sc_time_stamp() << " Hardware served as master reading data from memory now...... " << endl;
      Bus_to_hardware_master->Request(1, addr_read, Mode_R, MEM_abc_length);
      cycle += 2;

      cout << " @" << sc_time_stamp() << " Hardware waiting acknowledgedment... " << endl;
      Ack = Bus_to_hardware_master->WaitForAcknowledge(1);
      cycle += 2;

      if (Ack)
      {
        cout << " @" << sc_time_stamp() << " Hardware recieved acknowledgedment... " << endl;
        for (int i = 0; i <MEM_abc_length; i++)
        {
          Bus_to_hardware_master->ReadData(Register[i]); // total read times: MEM_abc_length (36)
          cycle += 2;
          if (i == 0)
            cout << " @" << sc_time_stamp() << " Hardware reading...." << i+1 << endl;
          else if (i ==35)
            cout << " @" << sc_time_stamp() << " Hardware reading...." << i+1 << endl;
        }
      }
      // else
      //   cout << " @" << sc_time_stamp() << " Hardware Read is not acknowledged" << endl;
      cout << " @" << sc_time_stamp() << " Hardware Read from memory successfully...." << endl;
    }


    void HW_Write_To_Memory(unsigned addr_read, unsigned int Register[MEM_abc_length])
    {
      cout << " @" << sc_time_stamp() << " Hardware served as master writing data to memory now...... " << endl;
      Bus_to_hardware_master->Request(1, addr_read, Mode_W, MEM_abc_length);
      cycle += 2;
      cout << " @" << sc_time_stamp() << " Hardware waiting acknowledgedment... " << endl;
      Ack = Bus_to_hardware_master->WaitForAcknowledge(1);
      cycle += 2;

      if (Ack)
      {
        cout << " @" << sc_time_stamp() << " Hardware recieved acknowledgedment... " << endl;
        for (int i = 0; i<MEM_abc_length; i++)
        {
          Bus_to_hardware_master->WriteData(Register[i]);  // total write times: MEM_abc_length (36)
          cycle += 1;
          if (i == 0)
            cout << " @" << sc_time_stamp() << " Hardware writing...." << i+1 << endl;
          else if (i ==35)
            cout << " @" << sc_time_stamp() << " Hardware writing...." << i+1 << endl;
        }  
    
      }
      // else
      //   cout << " @" << sc_time_stamp() << " Hardware write is not acknowledged" << endl;
      cout << " @" << sc_time_stamp() << " Hardware write to memory successfully......" << endl;
    }
};

// Software Always Master : masterid: 0
// keep initalization to software
// tsoftware
class Software: public sc_module
{
  public: 
    // port to bus
    sc_port<bus_master_if> Bus_to_software;
    sc_in<sc_logic> Clk;

    // int n;
    // int i,j,k;

    unsigned int MEM[MEM_SIZE];
    bool Ack= false;
    
    SC_HAS_PROCESS(Software);

    // constructor
    Software (sc_module_name name) : sc_module(name)
    {
      //cout << " @" << sc_time_stamp() << " Software Thread starts" << endl;
      SC_THREAD(Software_cal);
        sensitive << Clk.pos();
    }

    void Software_initialize()
    {
      cout << " @" << sc_time_stamp() << " Start doing software initalization...." << endl;
      Bus_to_software->Request(0, MEM_c_ADDR, Mode_W, MEM_abc_length);  
      cycle += 2;
      cout << " @" << sc_time_stamp() << " Software waiting acknowledgedment" << endl;
      Ack = Bus_to_software->WaitForAcknowledge(0);
      cycle += 2;
      if (Ack)
      {        
        cout << " @" << sc_time_stamp() << " Software get acknowledgedment" << endl;
        // write to MEM[MEM_c_ADDR+i]  // initalize c here
        for(int i=0; i<=SIZE; i++) // Total Cycles: 579000, Execs: 1000, Iters: 5  
          for(int j=0; j<=SIZE; j++) // Total Cycles: 520000, Execs: 5000, Iters: 5
            {
              Bus_to_software->WriteData(0);
              cycle += 1;
            }
        cout << " @" << sc_time_stamp() << " Software finsihed initalization......" << endl;
      }
      else 
        cout << " @" << sc_time_stamp() << " Software is still waiting for Acknowledge" << endl;
      
    }  

    // requesting hardware
    void Software_call_Hardware()
    {
      cout << " @" << sc_time_stamp() << " Software serves as master calling hardware to cooperate......" << endl;
      Bus_to_software->Request(0, ADDR_Calling_hardware, Mode_Cal, 1);
      cycle += 2;
      cout << " @" << sc_time_stamp() << " Software waiting acknowledgedment" << endl;
      Bus_to_software->WaitForAcknowledge(0);
      cycle += 2;
    }

    void Software_cal()
    {
      // doing 1000 times
      // LOOPS

      for(int n=0; n<LOOPS; n++)
      {
        double c1 = cycle; 
        cout << " @" << sc_time_stamp() << " " << n+1 << "-time Calculation..." << endl;
        Software_initialize();
        Software_call_Hardware();

        // wait( (5 * 2/3) * 348, SC_NS);
        // double c2 = cycle; 
        // double CD; 
        // cout << CD << endl;

        // // a = 1577.455;
        // double a = 193.5 *clock_period;
        wait(194 * clock_period , SC_NS);
        // cout << " @" << sc_time_stamp() << " finish all!" << endl;
        cout << " ======================================================================" << endl;
        cout << " ======================================================================" << endl;
        cycle += 2;
      }
      // job finished
      sc_stop();
    }

};



class top : public sc_module
{
  public:
    sc_signal<sc_logic> Clk_sig;

    Memory memory_inst;
    Hardware hardware_inst;
    Software software_inst;
    Bus Bus_inst;

    OSCILLATOR oscillator_inst;


    top(sc_module_name name) : sc_module(name), 
                               memory_inst("Memory"),
                               hardware_inst("Hardware"),
                               software_inst("Software"),
                               Bus_inst("Bus"),
                               oscillator_inst("oscillator")
    {
        // connect to bus
        hardware_inst.Bus_to_hardware_minion(Bus_inst);
        hardware_inst.Bus_to_hardware_master(Bus_inst);
        software_inst.Bus_to_software(Bus_inst);
        memory_inst.Bus_to_memory(Bus_inst);

        //block input Clk from oscillator
        hardware_inst.Clk(Clk_sig);
        software_inst.Clk(Clk_sig);
        memory_inst.Clk(Clk_sig);
        Bus_inst.Clk(Clk_sig);

        //oscillator output Clk
        oscillator_inst.Clk_out(Clk_sig);
        // cout << " @" << sc_time_stamp() << " All blocks connect to Clk ....." << endl;
    }
};        


int sc_main(int argc, char* argv[])
{
    top top("Top");
    sc_start();

    top.memory_inst.print_result();

    // print golden answer 
    int a[MEM_abc_length] = { 0,0,0,0,0,0,0,0,9,4,7,9,0,12,14,15,16,11,0,2,3,4,5,6,0,4,3,2,1,2,0,2,7,6,4,9 };
    int b[MEM_abc_length] = { 0,0,0,0,0,0,0,0,9,4,7,9,0,12,14,15,16,11,0,2,3,4,5,6,0,4,3,2,1,2,0,2,7,6,4,9 };
    int c[MEM_abc_length] = {0};
    // for (int i =0; i< SIZE; i++)
    //   for (int j =0; i< SIZE; i++)
    //     for (int k =0; i< SIZE; i++)
    //         c[i*MEM_length_row_column+j*MEM_length_row_column] += a[i * MEM_length_row_column + k] * b[k * MEM_length_row_column + j];

    for (int i = 0; i<=SIZE; i++) //row 
    {
      for (int j = 0; j<=SIZE; j++) //column
      {
        unsigned int C_Sum = 0;
        for (int k = 0; k <=SIZE; k++)
        {
          //c[i][j] += a[i][k] * b[k][j];
          C_Sum = C_Sum + a[i * MEM_length_row_column + k] * b[k * MEM_length_row_column + j];
        }
        c[i * MEM_length_row_column +j] = C_Sum;
      }
    }

    cout << "Golden value is" << endl; 
    for(int i =0; i<MEM_abc_length; i++)            
          cout << (int) c[i] << ", ";


    cout << endl;
    cout << " Toatl simulation time: " << std::setprecision(7)<< sc_time_stamp().to_seconds()*1e9 << "ns" <<endl;
    cout << " Total Cycle is (by count): " << cycle << endl; 
    cout << " Total cycle is (by toatl_simulation_time/clock period): " << std::setprecision(10)<< sc_time_stamp().to_seconds()*1e9/(clock_period) << " cycle."<<endl;
    cout << " Each iteration takes " << std::setprecision(7)<< sc_time_stamp().to_seconds()*1e6/(clock_period) << " cycle."<<endl;
    return 0;
}

    // sc_trace_file *wf = sc_create_vcd_trace_file("WaveForm");
    // sc_trace(wf, top1.Clk_sig, "Clk");
    // sc_start();
    // sc_close_vcd_trace_file(wf);


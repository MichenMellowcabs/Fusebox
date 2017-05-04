/*
 * User_Functions.c
 *
 *  Created on: 04 May 2017
 *      Author: Sonja
 */

#include "User_Defines.h"

void Initialise_BMS(void)
{
    flagCurrent = 0;

    InitSysCtrl();
    InitI2CGpio();
    Init_Gpio();
    InitAdc();
    InitSpiaGpio();

    DINT;

    InitPieCtrl();

    IER = 0x0000;
    IFR = 0x0000;

    InitPieVectTable();

    EALLOW; // This is needed to write to EALLOW protected registers
    PieVectTable.I2CINT1A = &i2c_int1a_isr;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    PieVectTable.TINT1 = &cpu_timer1_isr;
    PieVectTable.TINT2 = &cpu_timer2_isr;
    PieVectTable.ADCINT1 = &adc_isr;
    EDIS;   // This is needed to disable write to EALLOW protected registers

    I2CA_Init();
    InitCpuTimers();

    ConfigCpuTimer(&CpuTimer0, 60, 500000); //2 hz
    ConfigCpuTimer(&CpuTimer1, 60, 20000);  //50 hz
    ConfigCpuTimer(&CpuTimer2, 60, 500);    //2 Khz         //500

    CpuTimer0Regs.TCR.all = 0x4000; // Use write-only instruction to set TSS bit = 0
    CpuTimer1Regs.TCR.all = 0x4000; // Use write-only instruction to set TSS bit = 0
    CpuTimer2Regs.TCR.all = 0x4000; // Use write-only instruction to set TSS bit = 0

    // Enable ADC interrupt 10 in the PIE: Group 10 interrupt 1
    PieCtrlRegs.PIEIER10.bit.INTx1 = 1;
    IER |= M_INT10;

    // Enable CPU interrupt 1 in the PIE: Group 1 interrupt 7
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1;          //clock
    IER |= M_INT1;
    IER |= M_INT13;
    IER |= M_INT14;

    // Enable I2C interrupt 1 in the PIE: Group 8 interrupt 1
    PieCtrlRegs.PIEIER8.bit.INTx1 = 1;
    // Enable I2C INT8 which is connected to PIE group 8
    IER |= M_INT8;

    EnableInterrupts();

    EINT;
    ERTM;   // Enable Global realtime interrupt DBGM

    CAN_Init();
    configADC();
    Bq76940_Init();
    //  Shut_D_BQ();
}

void Init_Gpio(void)
{
    EALLOW;

    GpioCtrlRegs.GPAMUX1.bit.GPIO3 = 0;     //BQenable
    GpioCtrlRegs.GPADIR.bit.GPIO3 = 1;      //Bq enable

    GpioCtrlRegs.GPAMUX1.bit.GPIO4 = 0;     //led2
    GpioCtrlRegs.GPADIR.bit.GPIO4 = 1;      //led2

    GpioCtrlRegs.GPAMUX1.bit.GPIO5 = 0;     //led1
    GpioCtrlRegs.GPADIR.bit.GPIO5 = 1;      //led1

    GpioCtrlRegs.GPAMUX1.bit.GPIO6 = 0;     //BT reset
    GpioCtrlRegs.GPADIR.bit.GPIO6 = 1;      //BT reset
    BTReset = 0;                            //keep BT in reset

    GpioCtrlRegs.GPAMUX1.bit.GPIO15 = 0;    //12 Aux drive
    GpioCtrlRegs.GPADIR.bit.GPIO15 = 1;     // 12 Aux drive (verander miskien)
    Aux_Control = 1;

    GpioCtrlRegs.GPAMUX2.bit.GPIO19 = 0;    //KeyDrive
    GpioCtrlRegs.GPADIR.bit.GPIO19 = 0;     //(input) key drive (verander miskien)

    GpioCtrlRegs.GPAMUX2.bit.GPIO20 = 0;    //contactor output
    GpioCtrlRegs.GPADIR.bit.GPIO20 = 1;     // contactor output
    ContactorOut = 0;                       //turn off contactor

    GpioCtrlRegs.GPAMUX2.bit.GPIO21 = 0;    //precharge resistor
    GpioCtrlRegs.GPADIR.bit.GPIO21 = 1;     // precharge resistor
    PreCharge = 1;                          //turn on precharge resistor

    GpioCtrlRegs.GPAMUX2.bit.GPIO24 = 0;    //key switch
    GpioCtrlRegs.GPADIR.bit.GPIO24 = 0;     //key switch

    GpioCtrlRegs.GPAMUX2.bit.GPIO26 = 0;    //BQ on
    GpioCtrlRegs.GPADIR.bit.GPIO26 = 0;     //BQ on (input)

    GpioCtrlRegs.GPAMUX2.bit.GPIO27 = 0;    //CANenable
    GpioCtrlRegs.GPADIR.bit.GPIO27 = 1;     //CANenable (output)

    GpioCtrlRegs.GPBMUX1.bit.GPIO40 = 0;    //led3
    GpioCtrlRegs.GPBDIR.bit.GPIO40 = 1;     //led3

    GpioCtrlRegs.GPBMUX1.bit.GPIO44 = 0;    //CScontrol
    GpioCtrlRegs.GPBDIR.bit.GPIO44 = 1;     //CSControl

    EDIS;

    //turn on contactor
    ContactorOut = 1;

    CSControl = 0;  //turn CScontrol on for current measurement
}

void Toggle_LED(void)
{
    GpioDataRegs.GPATOGGLE.bit.GPIO5 = 1;
}

void  Read_Cell_Voltages(void)
{
    // Read data from EEPROM section //
    int i;
    Voltage_total = 0;
    Voltage_high = 0;                      //reset values
    Voltage_low = 10;

    float temp_V = 0;

    for(i = 0; i<15; i++)
    {
        temp_V = I2CA_ReadData(&I2cMsgIn1,0x0C+(i*0x02), 2);
        temp_V = (ADCgain * temp_V) + ADCoffset;

        Voltages[i] = temp_V;

        Voltage_total = Voltage_total +  Voltages[i];

        if(Voltage_high<Voltages[i])
            Voltage_high = Voltages[i];

        if(Voltage_low>Voltages[i])
            Voltage_low = Voltages[i];
    }
}

void Process_Voltages(void)
{
    if(Voltage_high > 3.6)         //3.65
    {
        balance = 1;            //start balancing
        flagCharged = 1;        //charged flag to to stop charging
    }

    if(Voltage_low < 2.8 && Voltage_low > 2.6)
    {
        flagDischarged = 1;
        led3 = 1;               //turn on red led
        ContactorOut = 0;       //turn off contactor            //turn off output
    }
    else if(Voltage_low < 2.6)
    {
        flagDischarged = 2;
    }

    if(Voltage_high<3.35 )
        flagCharged = 0;

    if(Voltage_low>2.8 )
        flagDischarged = 0;
}

void Calculate_Current(void)
{
    Current = (test_current-2085)* 0.125;                   //2035    maal, moenie deel nie!!!!     0.0982--200/2048
}

void Read_System_Status(void)
{
    system_status = I2CA_ReadData(&I2cMsgIn1,0x00, 1);

}

void Process_System_Status(void)
{
    if(system_status != 00)
    {
        I2CA_WriteData(0x00, 0x3F);
    }
}

void Read_Temperatures(void)
{
    int i;
    int flag = 0;
    float Vts;
    float Rts;

    float temp_T = 0;

    for(i = 0; i<3; i++)
    {
        temp_T = I2CA_ReadData(&I2cMsgIn1, 0x2C+(i*0x02), 2);
        //      test_blah[i] = T[i];

        if(i == 0)
            Vts = (temp_T*ADCgain) + 0.27;
        else
            Vts = temp_T*ADCgain;

        //test1 = Vts;
        Rts = (10000*Vts)/(3.3-Vts);
        //test2 = Rts;

        Temperatures[i] = (1/((log(Rts/10000))/4000+0.003356))-273;
        //  T[i] = T[i] -273;

        if(Temperatures[i]> 70 || Temperatures[i]<0)
        {
            flag = 1;
        }
    }

    if(flag == 1)
        flagTemp = 1;
    else if(flag == 0)
        flagTemp = 0;
}

void Balance(int period, float reference)
{
    static float count = 0;
    float Cell_B_Voltage = reference;
    int i;

    if(balance == 1)
    {
        if(count ==0)       // 4 siklusse met onewe selle
        {
            //reset all balancing
            I2CA_WriteData(0x01,0x00);
            I2CA_WriteData(0x02,0x00);
            I2CA_WriteData(0x03,0x00);
            Cell_B1 = 0;
            Cell_B2 = 0;
            Cell_B3 = 0;

            //setup balancing
            for(i = 0; i<5; i+=2)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {
                    Cell_B1 = (Cell_B1 | (0x01 << i));
                }
            }

            for(i = 5; i<10; i+=2)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {

                    Cell_B2 = (Cell_B2 | (0x01 << (i-5)));
                }
            }

            for(i = 10; i<15; i+=2)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {

                    Cell_B3 = (Cell_B3 | (0x01 << (i-10)));
                }
            }

            if(Cell_B1==0x0 && Cell_B2==0x0 && Cell_B3==0x0)
            {
                count = period;
            }
            else
            {
                I2CA_WriteData(0x01,Cell_B1);
                I2CA_WriteData(0x02,Cell_B2);
                I2CA_WriteData(0x03,Cell_B3);
                count++;
            }

        }
        else if(count ==period) // 4 siklusse met ewe selle
        {
            //reset all balancing
            I2CA_WriteData(0x01,0x00);
            I2CA_WriteData(0x02,0x00);
            I2CA_WriteData(0x03,0x00);
            Cell_B1 = 0;
            Cell_B2 = 0;
            Cell_B3 = 0;

            //setup balancing
            for(i = 1; i<5; i+=2)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {
                    Cell_B1 = (Cell_B1 | (0x01 << i));
                }
            }

            for(i = 6; i<10; i+=2)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {

                    Cell_B2 = (Cell_B2 | (0x01 << (i-5)));
                }
            }

            for(i = 11; i<15; i+=2)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {

                    Cell_B3 = (Cell_B3 | (0x01 << (i-10)));
                }
            }

            if(Cell_B1==0x0 && Cell_B2==0x0 && Cell_B3==0x0)
            {
                count = period*2;
            }
            else
            {
                I2CA_WriteData(0x01,Cell_B1);
                I2CA_WriteData(0x02,Cell_B2);
                I2CA_WriteData(0x03,Cell_B3);
                count++;
            }
        }
        else if(count == period*2)
        {
            balance = 0;

            for(i = 0; i<15; i++)
            {
                if(Voltages[i] > Cell_B_Voltage)
                {
                    balance++;
                }
            }

            if(balance !=0)
            {
                balance = 1;
            }
            else if(balance ==0)                //sit miskien else hier             hierdie is toets fase
            {
                balance = 0;
            }

            count = 0;
            I2CA_WriteData(0x01,0x00);
            I2CA_WriteData(0x02,0x00);
            I2CA_WriteData(0x03,0x00);
            Cell_B1 = 0;
            Cell_B2 = 0;
            Cell_B3 = 0;
        }
        else
        {
            count++;
        }
    }
}

unsigned char CRC8(unsigned char *ptr, unsigned char len,unsigned char key)
{
    unsigned char i;
    unsigned char crc=0;
    while(len--!=0)
    {
        for(i=0x80; i!=0; i/=2)
        {
            if((crc & 0x80) != 0)
            {
                crc *= 2;
                crc ^= key;
            }
            else
                crc *= 2;

            if((*ptr & i)!=0)
                crc ^= key;
        }
        ptr++;
    }
    return(crc);
}

Uint32 ChgCalculator(float Voltage, float Current)
{
    Uint32 answer= 0;
    Uint16 temp1 = 0;

    Voltage = Voltage*10;
    Current = Current*10;

    temp1 = (Uint16)Current;

    answer = answer | (temp1&0xFF);
    answer = (answer<<8) | (temp1>>8);

    temp1 = (Uint16)Voltage;

    answer = answer<<8 | (temp1&0xFF);
    answer = (answer<<8) | (temp1>>8);

    return answer;
}


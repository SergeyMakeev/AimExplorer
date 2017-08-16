using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Xml;
using OxyPlot;
using OxyPlot.Series;
using System.ComponentModel;
using System.Windows.Threading;
using System.Threading;

namespace Visualizer
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        XmlProcess xmlProcess = null;
        UInt64 addr01 = 0;
        UInt64 addr02 = 0;

        PlotViewModel mdl = new PlotViewModel();

        bool captureStarted = false;
        DispatcherTimer timer = new DispatcherTimer(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(600) };

        NativeMethods.CapturePacket[] packets = new NativeMethods.CapturePacket[16384];

        public MainWindow()
        {
            InitializeComponent();

            chart.Model = mdl.model;

            timer.Tick += Timer_Tick;
        }

        private void UpdateCharts()
        {
            UInt32 count = NativeMethods.GetCapturedData(packets, (UInt32)packets.Length);
            for (UInt32 i = 0; i < count; i++)
            {
                mdl.Update(packets[i]);
            }

            if (count > 0)
            {
                mdl.Invalidate();
            }
        }

        private void Timer_Tick(object sender, EventArgs e)
        {
            UpdateCharts();
        }

        private void MenuExit_Click(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void MenuExport_Click(object sender, RoutedEventArgs e)
        {
            Microsoft.Win32.SaveFileDialog dlg = new Microsoft.Win32.SaveFileDialog();
            dlg.DefaultExt = ".txt";
            dlg.Filter = "Excel text Files (*.txt)|*.txt|All Files (*.*)|*.*";
            Nullable<bool> result = dlg.ShowDialog();

            if (result == true)
            {
                string filename = dlg.FileName;
                mdl.ExportToExcel(filename);
            }
        }


        private void MenuImport_Click(object sender, RoutedEventArgs e)
        {
            //
            Microsoft.Win32.OpenFileDialog dlg = new Microsoft.Win32.OpenFileDialog();
            dlg.DefaultExt = ".xml";
            dlg.Filter = "XML Files (*.xml)|*.xml|All Files (*.*)|*.*";
            Nullable<bool> result = dlg.ShowDialog();

            if (result == true)
            {
                string filename = dlg.FileName;
                ImportXML(filename);
            }
        }

        void ImportXML(string filename)
        {
            List<XmlRotationComponentSuspect> suspects = new List<XmlRotationComponentSuspect>();

            XmlReaderSettings settings = new XmlReaderSettings();
            settings.ConformanceLevel = ConformanceLevel.Fragment;
            using (XmlReader reader = XmlReader.Create(filename, settings))
            {
                while (reader.Read())
                {
                    if (reader.IsStartElement())
                    {
                        switch (reader.Name)
                        {
                            case "process":
                                string sPid = reader["pid"];
                                string sPath = reader["path"];
                                string sBase = reader["base"];
                                xmlProcess = new XmlProcess();
                                xmlProcess.pid = UInt32.Parse(sPid);
                                xmlProcess.path = sPath;
                                xmlProcess.baseAddr = UInt64.Parse(sBase);
                                break;
                            case "item":
                                string sAddr = reader["addr"];
                                string sVal = reader["val"];
                                string sMin = reader["min"];
                                string sMax = reader["max"];
                                string sType = reader["type"];

                                XmlRotationComponentSuspect suspect = new XmlRotationComponentSuspect();
                                suspect.addr = UInt64.Parse(sAddr);
                                suspect.val = float.Parse(sVal);
                                suspect.val_min = float.Parse(sMin);
                                suspect.val_max = float.Parse(sMax);
                                suspect.type = UInt32.Parse(sType);
                                suspects.Add(suspect);
                                break;
                        }

                    }
                }
            }

            Process targetProcess = Process.GetProcessById((int)xmlProcess.pid);
            if (targetProcess == null)
            {
                MessageBox.Show(this, "Can't open process " + xmlProcess.pid);
                return;
            }

            UInt64 baseAddr = (UInt64)targetProcess.MainModule.BaseAddress;
            if (xmlProcess.baseAddr != baseAddr)
            {
                MessageBox.Show(this, "Process base address is not equal! Another session?");
                return;
            }

            if (!targetProcess.MainModule.FileName.Equals(xmlProcess.path, StringComparison.InvariantCultureIgnoreCase))
            {
                MessageBox.Show(this, "Process path is not equal!\n" + targetProcess.MainModule.FileName + "\n" + xmlProcess.path + "\nAnother session ? ");
                return;
            }

            ImportWindow importWindow = new ImportWindow();
            importWindow.Owner = this;
            importWindow.SetData(xmlProcess, suspects);
            importWindow.ShowDialog();

            if (importWindow.importResults != null)
            {
                mdl.Clear();

                // Only two counters supported!!

                if (importWindow.importResults.Count >= 1)
                {
                    addr01 = importWindow.importResults[0].addr;
                    mdl.AddLine(importWindow.importResults[0]);
                }

                if (importWindow.importResults.Count >= 2)
                {
                    addr02 = importWindow.importResults[1].addr;
                    mdl.AddLine(importWindow.importResults[1]);
                }

                mdl.Invalidate();
                /*
                                if (mdl.OpenProcess((int)xmlProcess.pid))
                                {
                                    foreach(GraphItem item in importWindow.importResults)
                                    {
                                        mdl.AddLine(item);
                                    }
                                }
                */

                mdl.SetTitle(System.IO.Path.GetFileNameWithoutExtension(xmlProcess.path) + ", pid :" + xmlProcess.pid);
                mdl.SetSubTitle("Capture paused");
            }
        }

        private void Window_Closed(object sender, EventArgs e)
        {
        }

        private void buttonCapture_Click(object sender, RoutedEventArgs e)
        {
            if (xmlProcess == null)
            {
                MessageBox.Show("Import some data first!", "ERROR");
                return;
            }

            if (captureStarted == true)
            {
                UpdateCharts();
                NativeMethods.StopEtwSession();
                timer.Stop();
                captureButton.Content = "Start Capture";
                mdl.SetSubTitle("Capture paused");
                captureStarted = false;
                mdl.Invalidate();
            }
            else
            {
                mdl.ClearData();

                Process targetProcess = Process.GetProcessById((int)xmlProcess.pid);
                NativeMethods.SetForegroundWindow(targetProcess.MainWindowHandle);
                Thread.Sleep(1500);

                UInt32 err = NativeMethods.StartEtwSession(xmlProcess.pid, addr01, addr02);
                if (err == 0)
                {
                    timer.Start();
                    captureButton.Content = "Stop Capture";
                    mdl.SetSubTitle("Capture in progress");
                    captureStarted = true;
                    mdl.Invalidate();
                }
                else
                {
                    MessageBox.Show("Can't start ETW session! Error code : " + err.ToString(), "ERROR");
                    if (err == 1)
                    {
                        NativeMethods.StopEtwSession();
                    }
                }
            }
        }

        private void ScanProcess_Click(object sender, RoutedEventArgs e)
        {
            ProcessSelector selector = new ProcessSelector();
            selector.Owner = this;
            selector.ShowDialog();
        }
    }


    public enum ComponentDataType
    {
        ANGLE_RAD,
        ANGLE_DEG,
        SKIP,
        RAW
    }

    public class XmlProcess
    {
        public UInt32 pid;
        public string path;
        public UInt64 baseAddr;
    }

    public class XmlRotationComponentSuspect
    {
        public UInt64 addr;
        public float val;
        public float val_min;
        public float val_max;
        public UInt32 type;
    }

    public class GraphItem : INotifyPropertyChanged
    {
        public LineSeries oxyLine = null;

        public UInt32 type;
        public ComponentDataType dataType;
        float v;
        public float lastSeenValue;

        public bool import { get; set; }
        public UInt64 addr { get; set; }
        public float min { get; set; }
        public float max { get; set; }
        public string DataType
        {
            get
            {
                switch (dataType)
                {
                    case ComponentDataType.ANGLE_DEG:
                        return "ANGLE_DEG";
                    case ComponentDataType.ANGLE_RAD:
                        return "ANGLE_RAD";
                    case ComponentDataType.RAW:
                        return "RAW";
                }

                return "SKIP";
            }

            set
            {
                switch (value)
                {
                    case "ANGLE_DEG":
                        dataType = ComponentDataType.ANGLE_DEG;
                        break;
                    case "ANGLE_RAD":
                        dataType = ComponentDataType.ANGLE_RAD;
                        break;
                    case "RAW":
                        dataType = ComponentDataType.RAW;
                        break;
                    default:
                        dataType = ComponentDataType.SKIP;
                        break;
                }
            }
        }
        public float value
        {
            get
            {
                return v;
            }

            set
            {
                v = value;
                OnPropertyChanged("value");
                OnPropertyChanged("DegValue");
            }
        }

        public float DegValue
        {
            get
            {
                switch (dataType)
                {
                    case ComponentDataType.ANGLE_DEG:
                        return v;
                    case ComponentDataType.ANGLE_RAD:
                        return v * ImportWindow.RAD_TO_DEG;
                }
                return v;
            }
        }

        public string RCategory
        {
            get
            {
                switch (type)
                {
                    case 0:
                        return "UNKNOWN";
                    case 1:
                        return "YAW";
                    case 2:
                        return "PITCH";
                    case 3:
                        return "BOTH";
                }

                return "ERROR";
            }
        }

        public string Name
        {
            get; set;
        }



        public event PropertyChangedEventHandler PropertyChanged;
        private void OnPropertyChanged(string propertyName)
        {
            if (PropertyChanged != null)
                PropertyChanged(this, new PropertyChangedEventArgs(propertyName));
        }

    }


    public static class NativeMethods
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct MEMORY_BASIC_INFORMATION
        {
            public IntPtr BaseAddress;
            public IntPtr AllocationBase;
            public uint AllocationProtect;
            public IntPtr RegionSize;
            public uint State;
            public uint Protect;
            public uint Type;
        }

        public const uint PAGE_EXECUTE = 0x00000010;
        public const uint PAGE_EXECUTE_READ = 0x00000020;
        public const uint PAGE_EXECUTE_READWRITE = 0x00000040;
        public const uint PAGE_EXECUTE_WRITECOPY = 0x00000080;
        public const uint PAGE_NOACCESS = 0x00000001;
        public const uint PAGE_READONLY = 0x00000002;
        public const uint PAGE_READWRITE = 0x00000004;
        public const uint PAGE_WRITECOPY = 0x00000008;
        public const uint PAGE_GUARD = 0x00000100;
        public const uint PAGE_NOCACHE = 0x00000200;
        public const uint PAGE_WRITECOMBINE = 0x00000400;


        public const uint MEM_COMMIT = 0x00001000;
        public const int PROCESS_WM_READ = 0x0010;
        public const int PROCESS_QUERY_INFORMATION = 0x0400;

        [DllImport("kernel32.dll")]
        public static extern IntPtr OpenProcess(int dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern Int32 ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, [In, Out] byte[] buffer, UInt32 size, out IntPtr lpNumberOfBytesRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);


        [StructLayout(LayoutKind.Sequential)]
        public struct CapturePacket
        {
            public Int64 eventTimeStamp;
            public Int64 captureTimeStamp;
            public Int64 freq;
            public Int32 frameIndex;
            public float gamepadX;
            public float gamepadY;
            public float val01;
            public float val02;
        };



        [DllImport("VisualizerNative.dll")]
        public static extern UInt32 StartEtwSession(UInt32 targetProcessId, UInt64 addr01, UInt64 addr02);

        [DllImport("VisualizerNative.dll")]
        public static extern UInt32 StopEtwSession();

        [DllImport("VisualizerNative.dll")]
        public static extern UInt32 GetCapturedData([In, Out] CapturePacket[] buffer, UInt32 bufferSizeInElements);
        
    }


}

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;


namespace Visualizer
{
    /// <summary>
    /// Interaction logic for ImportWindow.xaml
    /// </summary>
    public partial class ImportWindow : Window
    {
        public static float DEG_TO_RAD = 0.01745329251f;
        public static float RAD_TO_DEG = 57.2957795131f;


        byte[] buffer = new byte[4];
        byte[] bufferMt = new byte[4];
        IntPtr targetProcessHandle = IntPtr.Zero;
        System.Windows.Threading.DispatcherTimer timer = new System.Windows.Threading.DispatcherTimer();
        List<string> dataTypes = new List<string>();
        List<GraphItem> importData;

        public List<GraphItem> importResults;

        private ComponentDataType DetectType(XmlRotationComponentSuspect suspect, out string name)
        {
            if ((suspect.val_min >= -180.0f && suspect.val_min < -160.0f) &&
                (suspect.val_max > 160.0f && suspect.val_max <= 180.0f))
            {
                name = "yaw";
                return ComponentDataType.ANGLE_DEG;
            }

            if ((suspect.val_min >= 0.0f && suspect.val_min < 20.0f) &&
                (suspect.val_max > 340.0f && suspect.val_max <= 360.0f))
            {
                name = "yaw";
                return ComponentDataType.ANGLE_DEG;
            }

            if ((suspect.val_min >= 0.0f * DEG_TO_RAD && suspect.val_min < 20.0f * DEG_TO_RAD) &&
                (suspect.val_max > 340.0f * DEG_TO_RAD && suspect.val_max <= 360.0f * DEG_TO_RAD))
            {
                name = "yaw";
                return ComponentDataType.ANGLE_RAD;
            }


            if ((suspect.val_min >= -180.0f * DEG_TO_RAD && suspect.val_min < -160.0f * DEG_TO_RAD) &&
                (suspect.val_max > 160.0f * DEG_TO_RAD && suspect.val_max <= 180.0f * DEG_TO_RAD))
            {
                name = "yaw";
                return ComponentDataType.ANGLE_RAD;
            }
                

            if ((suspect.val_min >= -90.0f && suspect.val_min < -60.0f) &&
                (suspect.val_max > 60.0f && suspect.val_max <= 90.0f))
            {
                name = "pitch";
                return ComponentDataType.ANGLE_DEG;
            }

            if ((suspect.val_min >= -90.0f && suspect.val_min < -60.0f) &&
                (suspect.val_max > 60.0f && suspect.val_max <= 90.0f))
            {
                name = "pitch";
                return ComponentDataType.ANGLE_DEG;
            }

            if ((suspect.val_min >= -90.0f * DEG_TO_RAD && suspect.val_min < -60.0f * DEG_TO_RAD) &&
                (suspect.val_max > 60.0f * DEG_TO_RAD && suspect.val_max <= 90.0f * DEG_TO_RAD))
            {
                name = "pitch";
                return ComponentDataType.ANGLE_RAD;
            }


            name = suspect.addr.ToString();
            return ComponentDataType.SKIP;
        }


        public ImportWindow()
        {
            InitializeComponent();

            dataTypes.Add("ANGLE_DEG");
            dataTypes.Add("ANGLE_RAD");
            dataTypes.Add("SKIP");
            dataTypes.Add("RAW");
        }


        public bool SetData(XmlProcess process, List<XmlRotationComponentSuspect> suspects)
        {
            importData = new List<GraphItem>();
            foreach(XmlRotationComponentSuspect v in suspects)
            {
                string autoName;
                ComponentDataType autoType = DetectType(v, out autoName);

                importData.Add(new GraphItem() { import = false, addr = v.addr, min = v.val_min, max = v.val_max, dataType = autoType, value = v.val, type = v.type, Name = autoName, lastSeenValue = 0.0f });
            }

            dataGrid.ItemsSource = importData;
            DataTypeColumn.ItemsSource = dataTypes;

            targetProcessHandle = NativeMethods.OpenProcess(NativeMethods.PROCESS_WM_READ, false, (int)process.pid);
            if (targetProcessHandle == IntPtr.Zero)
            {
                return false;
            }

            ReadLastSeenValues();

            Title = "Import (" + importData.Count.ToString() + ")";

            int tickMs = 33;
            timer.Tick += new EventHandler(OnTimerTick);
            timer.Interval = new TimeSpan(tickMs * 10000);
            timer.Start();

            return true;
        }

        private void OnTimerTick(object sender, EventArgs e)
        {
            if (targetProcessHandle == IntPtr.Zero)
            {
                return;
            }

            IntPtr bytesRead;
            foreach (GraphItem item in importData)
            {

                if (checkBox.IsChecked == true && item.dataType == ComponentDataType.SKIP)
                {
                    continue;
                }

                NativeMethods.ReadProcessMemory(targetProcessHandle, (IntPtr)item.addr, buffer, (uint)buffer.Length, out bytesRead);
                if (bytesRead.ToInt64() == 4)
                {
                    float v = System.BitConverter.ToSingle(buffer, 0);
                    if (Math.Abs(item.value - v) > 0.001f)
                    {
                        item.value = v;
                    }

                }
            }
        }

        private void Window_Closed(object sender, EventArgs e)
        {
            if (targetProcessHandle != IntPtr.Zero)
            {
                NativeMethods.CloseHandle(targetProcessHandle);
                targetProcessHandle = IntPtr.Zero;
            }
        }

        private void Import_Click(object sender, RoutedEventArgs e)
        {
            List<GraphItem> res = new List<GraphItem>();
            foreach (GraphItem item in importData)
            {
                if (!item.import)
                {
                    continue;
                }

                res.Add(item);
            }

            if (res.Count > 0)
            {
                importResults = res;
            } else
            {
                importResults = null;
            }

            Close();
        }

        private void Cancel_Click(object sender, RoutedEventArgs e)
        {
            importResults = null;
            Close();
        }

        private void ReadLastSeenValues()
        {
            IntPtr bytesRead;
            for (int i = 0; i < importData.Count; i++)
            {
                GraphItem item = importData[i];

                NativeMethods.ReadProcessMemory(targetProcessHandle, (IntPtr)item.addr, bufferMt, (uint)bufferMt.Length, out bytesRead);
                if (bytesRead.ToInt64() == 4)
                {
                    item.lastSeenValue = System.BitConverter.ToSingle(bufferMt, 0);
                } else
                {
                    item.lastSeenValue = 0.0f;
                }
            }
        }

        private void RemoveChanged_Click(object sender, RoutedEventArgs e)
        {
            int removedCount = 0;
            IntPtr bytesRead;
            for(int i = 0; i < importData.Count; i++)
            {
                GraphItem item = importData[i];

                NativeMethods.ReadProcessMemory(targetProcessHandle, (IntPtr)item.addr, bufferMt, (uint)bufferMt.Length, out bytesRead);
                if (bytesRead.ToInt64() == 4)
                {
                    float valNow = System.BitConverter.ToSingle(bufferMt, 0);

                    if (Math.Abs(item.lastSeenValue - valNow) > 0.001f)
                    {
                        removedCount++;
                        importData.RemoveAt(i);
                        i--;
                        continue;
                    }
                    item.lastSeenValue = valNow;
                }
            }

            dataGrid.ItemsSource = null;
            dataGrid.InvalidateVisual();
            dataGrid.ItemsSource = importData;
            Title = "Import (" + importData.Count.ToString() + ")";
            //MessageBox.Show("removed " + removedCount.ToString());
        }

        private void RemoveUnchanged_Click(object sender, RoutedEventArgs e)
        {
            int removedCount = 0;
            IntPtr bytesRead;
            for (int i = 0; i < importData.Count; i++)
            {
                GraphItem item = importData[i];

                NativeMethods.ReadProcessMemory(targetProcessHandle, (IntPtr)item.addr, bufferMt, (uint)bufferMt.Length, out bytesRead);
                if (bytesRead.ToInt64() == 4)
                {
                    float valNow = System.BitConverter.ToSingle(bufferMt, 0);

                    if (item.lastSeenValue == valNow)
                    {
                        removedCount++;
                        importData.RemoveAt(i);
                        i--;
                        continue;
                    }
                    item.lastSeenValue = valNow;
                }
            }


            dataGrid.ItemsSource = null;
            dataGrid.InvalidateVisual();
            dataGrid.ItemsSource = importData;
            Title = "Import (" + importData.Count.ToString() + ")";
            //MessageBox.Show("removed " + removedCount.ToString());
        }

        private void checkBox_Checked(object sender, RoutedEventArgs e)
        {
            

        }
    }
}

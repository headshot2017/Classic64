using System;
using System.Collections.Generic;
using System.Threading;

using MCGalaxy.Events;
using MCGalaxy.Events.PlayerEvents;
using MCGalaxy.Events.ServerEvents;
using MCGalaxy.Levels;
using MCGalaxy.Network;
using MCGalaxy.Tasks;

enum MarioOpcodes
{
	OPCODE_MARIO_HAS_PLUGIN=1,
	OPCODE_MARIO_TICK,
	OPCODE_MARIO_SET_COLORS,
	OPCODE_MARIO_SET_CAP,
	OPCODE_MARIO_FORCE
}

public struct RGBCol
{
	public RGBCol(byte R, byte G, byte B) {r = R; g = G; b = B;}
	public byte r, g, b;
}

public struct SM64MarioTickData
{
	public ushort health;
	public uint action, actionArg;
	public short faceAngle;

	public float camLookX, camLookZ;
    public float stickX, stickY;
    public byte buttonA, buttonB, buttonZ;
}

public class marioInstance
{
	public marioInstance()
	{
		hasPlugin = false;
		cap = 0x00000001; // MARIO_NORMAL_CAP
		customColors = false;
		colors = new RGBCol[6];
		tickdata = new SM64MarioTickData();
	}

	public bool hasPlugin;

	public SM64MarioTickData tickdata;
	public uint cap;
	public bool customColors;
	public RGBCol[]	colors;
}

namespace MCGalaxy 
{
    public sealed class Classic64 : Plugin 
    {
        public override string name { get { return "Classic64"; } }
        public override string MCGalaxy_Version { get { return "1.9.4.0"; } }
        public override string creator { get { return "Headshotnoby"; } }

		public static Dictionary<string, marioInstance> marioSettings = new Dictionary<string, marioInstance>();
		public SchedulerTask marioTask;

		public override void Load(bool startup)
		{
			OnPluginMessageReceivedEvent.Register(OnPluginMessageReceived, Priority.Critical);
			OnPlayerConnectEvent.Register(OnPlayerConnect, Priority.Critical);
			OnPlayerDisconnectEvent.Register(OnPlayerDisconnect, Priority.Critical);
			OnJoinedLevelEvent.Register(OnJoinedLevel, Priority.Critical);
			OnSentMapEvent.Register(OnSentMap, Priority.Critical);

			ModelInfo.Models.Add(new ModelInfo("mario64", 10,10,10, 15));
			marioTask = Server.Critical.QueueRepeat(marioTick, null, TimeSpan.FromSeconds(1.0/30));
		}
		
		public override void Unload(bool shutdown)
		{
			OnPluginMessageReceivedEvent.Unregister(OnPluginMessageReceived);
			OnPlayerConnectEvent.Unregister(OnPlayerConnect);
			OnPlayerDisconnectEvent.Unregister(OnPlayerDisconnect);
			OnJoinedLevelEvent.Unregister(OnJoinedLevel);
			OnSentMapEvent.Unregister(OnSentMap);

			Server.Critical.Cancel(marioTask);

			foreach (ModelInfo m in ModelInfo.Models)
			{
				if (m.Model.CaselessEq("mario64"))
				{
					ModelInfo.Models.Remove(m);
					break;
				}
			}
		}



		static void sendColorsToEveryone(Player p)
		{
			byte[] data = new byte[64];

			data[0] = (byte)MarioOpcodes.OPCODE_MARIO_SET_COLORS;
			data[1] = p.EntityID;
			data[2] = (byte)(marioSettings[p.name].customColors ? 1 : 0);
			if (marioSettings[p.name].customColors)
			{
				for (int i=0; i<6; i++)
				{
					data[3 + (i*3) + 0] = marioSettings[p.name].colors[i].r;
					data[3 + (i*3) + 1] = marioSettings[p.name].colors[i].g;
					data[3 + (i*3) + 2] = marioSettings[p.name].colors[i].b;
				}
			}

			foreach (Player other in PlayerInfo.Online.Items)
			{
				if (other.level != p.level || p == other || !marioSettings[other.name].hasPlugin) continue; // only send to those in the same map
				other.Send(Packet.PluginMessage(64, data));
			}
		}

		static void sendCapToEveryone(Player p)
		{
			byte[] data = new byte[64];

			data[0] = (byte)MarioOpcodes.OPCODE_MARIO_SET_CAP;
			data[1] = p.EntityID;
			NetUtils.WriteI32((int)marioSettings[p.name].cap, data, 2);

			foreach (Player other in PlayerInfo.Online.Items)
			{
				if (other.level != p.level || p == other || !marioSettings[other.name].hasPlugin) continue; // only send to those in the same map
				other.Send(Packet.PluginMessage(64, data));
			}
		}

		static void receiveColorsFromEveryone(Player p)
		{
			if (!marioSettings[p.name].hasPlugin) return;

			foreach (Player other in PlayerInfo.Online.Items)
			{
				if (other.level != p.level || p == other || !marioSettings[other.name].hasPlugin) continue; // only send to those in the same map

				byte[] data = new byte[64];

				data[0] = (byte)MarioOpcodes.OPCODE_MARIO_SET_COLORS;
				data[1] = other.EntityID;
				data[2] = (byte)(marioSettings[other.name].customColors ? 1 : 0);
				if (marioSettings[other.name].customColors)
				{
					for (int i=0; i<6; i++)
					{
						data[3 + (i*3) + 0] = marioSettings[other.name].colors[i].r;
						data[3 + (i*3) + 1] = marioSettings[other.name].colors[i].g;
						data[3 + (i*3) + 2] = marioSettings[other.name].colors[i].b;
					}
				}

				p.Send(Packet.PluginMessage(64, data));
			}
		}

		static void receiveCapsFromEveryone(Player p)
		{
			if (!marioSettings[p.name].hasPlugin) return;

			foreach (Player other in PlayerInfo.Online.Items)
			{
				if (other.level != p.level || p == other || !marioSettings[other.name].hasPlugin) continue; // only send to those in the same map

				byte[] data = new byte[64];

				data[0] = (byte)MarioOpcodes.OPCODE_MARIO_SET_CAP;
				data[1] = other.EntityID;
				NetUtils.WriteI32((int)marioSettings[other.name].cap, data, 2);

				p.Send(Packet.PluginMessage(64, data));
			}
		}

		unsafe static void readTickData(Player p, byte[] data)
		{
			int camLookX = NetUtils.ReadI32(data, 13);
			int camLookZ = NetUtils.ReadI32(data, 17);
			int stickX = NetUtils.ReadI32(data, 21);
			int stickY = NetUtils.ReadI32(data, 25);

			marioSettings[p.name].tickdata.health = (ushort)NetUtils.ReadI16(data, 1);
			marioSettings[p.name].tickdata.action = (uint)NetUtils.ReadI32(data, 3);
			marioSettings[p.name].tickdata.actionArg = (uint)NetUtils.ReadI32(data, 7);
			marioSettings[p.name].tickdata.faceAngle = NetUtils.ReadI16(data, 11);
			marioSettings[p.name].tickdata.camLookX = *(float*)&camLookX;
			marioSettings[p.name].tickdata.camLookZ = *(float*)&camLookZ;
			marioSettings[p.name].tickdata.stickX = *(float*)&stickX;
			marioSettings[p.name].tickdata.stickY = *(float*)&stickY;
			marioSettings[p.name].tickdata.buttonA = data[29];
			marioSettings[p.name].tickdata.buttonB = data[30];
			marioSettings[p.name].tickdata.buttonZ = data[31];
		}

		static void marioTick(SchedulerTask task)
		{
			foreach (Player p in PlayerInfo.Online.Items)
			{
				if (!marioSettings.ContainsKey(p.name) || !marioSettings[p.name].hasPlugin) continue;

				foreach (Player other in PlayerInfo.Online.Items)
				{
					if (!marioSettings.ContainsKey(other.name) || other.level != p.level || p == other || !marioSettings[other.name].hasPlugin) continue;

					byte[] data = new byte[64];

					data[0] = (byte)MarioOpcodes.OPCODE_MARIO_TICK;
					data[1] = other.EntityID;
					NetUtils.WriteI16((short)marioSettings[other.name].tickdata.health, data, 2);
					NetUtils.WriteI32((int)marioSettings[other.name].tickdata.action, data, 4);
					NetUtils.WriteI32((int)marioSettings[other.name].tickdata.actionArg, data, 8);
					NetUtils.WriteI16(marioSettings[other.name].tickdata.faceAngle, data, 12);
					NetUtils.WriteF32(marioSettings[other.name].tickdata.camLookX, data, 14);
					NetUtils.WriteF32(marioSettings[other.name].tickdata.camLookZ, data, 18);
					NetUtils.WriteF32(marioSettings[other.name].tickdata.stickX, data, 22);
					NetUtils.WriteF32(marioSettings[other.name].tickdata.stickY, data, 26);
					data[30] = marioSettings[other.name].tickdata.buttonA;
					data[31] = marioSettings[other.name].tickdata.buttonB;
					data[32] = marioSettings[other.name].tickdata.buttonZ;

					p.Send(Packet.PluginMessage(64, data));
				}
			}
		}

		// begin event callbacks


		static void OnPlayerConnect(Player p)
		{
			marioSettings[p.name] = new marioInstance();
		}

		static void OnPlayerDisconnect(Player p, string reason)
		{
			marioSettings.Remove(p.name);
		}

		static void OnJoinedLevel(Player p, Level prevLevel, Level level, ref bool announce)
		{
			// send all Mario colors and caps
			if (p.Model.CaselessEq("mario64"))
			{
				sendColorsToEveryone(p);
				sendCapToEveryone(p);
			}
			receiveColorsFromEveryone(p);
			receiveCapsFromEveryone(p);
		}

		static void OnSentMap(Player p, Level prevLevel, Level level)
		{
			if (prevLevel == level)
			{
				// map was reloaded with /reload, or because 10000+ blocks were changed
				receiveColorsFromEveryone(p);
				receiveCapsFromEveryone(p);
			}
		}

		static void OnPluginMessageReceived(Player p, byte channel, byte[] data)
		{
			if (channel != 64) return;

			switch((MarioOpcodes)data[0])
			{
				case MarioOpcodes.OPCODE_MARIO_HAS_PLUGIN:
					marioSettings[p.name].hasPlugin = true;
					p.Send(Packet.PluginMessage(channel, data));
					break;

				case MarioOpcodes.OPCODE_MARIO_TICK:
					readTickData(p, data);
					break;

				case MarioOpcodes.OPCODE_MARIO_SET_COLORS:
					marioSettings[p.name].customColors = (data[1] == 1);
					for (int i=0; i<6; i++)
					{
						marioSettings[p.name].colors[i].r = data[2 + (i*3) + 0];
						marioSettings[p.name].colors[i].g = data[2 + (i*3) + 1];
						marioSettings[p.name].colors[i].b = data[2 + (i*3) + 2];
					}
					sendColorsToEveryone(p);
					break;

				case MarioOpcodes.OPCODE_MARIO_SET_CAP:
					marioSettings[p.name].cap = (uint)NetUtils.ReadI32(data, 1);
					sendCapToEveryone(p);
					break;

				case MarioOpcodes.OPCODE_MARIO_FORCE:
					{
						string[] motdParts = p.GetMotd().SplitSpaces();
						bool mario64 = false;
						for (int i=0; i<motdParts.Length; i++)
						{
							if (motdParts[i].CaselessEq("+mario64"))
							{
								mario64 = true;
								break;
							}
						}

						if (!mario64 && (!Hacks.CanUseHacks(p) || !Hacks.CanUseFly(p)))
						{
							p.Message("&cHacks are disabled, cannot switch to Mario");
							p.UpdateModel("humanoid");
							return;
						}

						byte on = data[1];
						p.UpdateModel((on == 1) ? "mario64" : "humanoid");
					}
					break;
			}
		}
	}
}

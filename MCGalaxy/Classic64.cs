using System;
using System.Collections.Generic;

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

public class marioInstance
{
	public marioInstance()
	{
		cap = 0x00000001; // MARIO_NORMAL_CAP
		customColors = false;
		colors = new RGBCol[6];
	}

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

		public override void Load(bool startup)
		{
			OnPluginMessageReceivedEvent.Register(OnPluginMessageReceived, Priority.Critical);
			OnPlayerConnectEvent.Register(OnPlayerConnect, Priority.Critical);
			OnPlayerDisconnectEvent.Register(OnPlayerDisconnect, Priority.Critical);
			OnJoinedLevelEvent.Register(OnJoinedLevel, Priority.Critical);
			OnSentMapEvent.Register(OnSentMap, Priority.Critical);
			ModelInfo.Models.Add(new ModelInfo("mario64", 10,10,10, 15));
		}
		
		public override void Unload(bool shutdown)
		{
			OnPluginMessageReceivedEvent.Unregister(OnPluginMessageReceived);
			OnPlayerConnectEvent.Unregister(OnPlayerConnect);
			OnPlayerDisconnectEvent.Unregister(OnPlayerDisconnect);
			OnJoinedLevelEvent.Unregister(OnJoinedLevel);
			OnSentMapEvent.Unregister(OnSentMap);

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
				if (other.level != p.level || p == other) continue; // only send to those in the same map
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
				if (other.level != p.level || p == other) continue; // only send to those in the same map
				other.Send(Packet.PluginMessage(64, data));
			}
		}

		static void receiveColorsFromEveryone(Player p)
		{
			foreach (Player other in PlayerInfo.Online.Items)
			{
				if (other.level != p.level || p == other) continue; // only send to those in the same map

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
			foreach (Player other in PlayerInfo.Online.Items)
			{
				if (other.level != p.level || p == other) continue; // only send to those in the same map

				byte[] data = new byte[64];

				data[0] = (byte)MarioOpcodes.OPCODE_MARIO_SET_CAP;
				data[1] = other.EntityID;
				NetUtils.WriteI32((int)marioSettings[other.name].cap, data, 2);

				p.Send(Packet.PluginMessage(64, data));
			}
		}

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
					p.Send(Packet.PluginMessage(channel, data));
					break;

				case MarioOpcodes.OPCODE_MARIO_TICK:
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

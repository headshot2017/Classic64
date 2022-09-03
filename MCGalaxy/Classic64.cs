using System;
using MCGalaxy.Events;
using MCGalaxy.Events.ServerEvents;
using MCGalaxy.Levels;
using MCGalaxy.Network;
using MCGalaxy.Tasks;

enum MarioOpcodes : byte
{
	OPCODE_MARIO_HAS_PLUGIN=1,
	OPCODE_MARIO_TICK,
	OPCODE_MARIO_SET_COLORS,
	OPCODE_MARIO_SET_CAP,
	OPCODE_MARIO_FORCE
}

namespace MCGalaxy 
{
    public sealed class Classic64 : Plugin 
    {
        public override string name { get { return "Classic64"; } }
        public override string MCGalaxy_Version { get { return "1.9.4.0"; } }
        public override string creator { get { return "Headshotnoby"; } }

		public override void Load(bool startup)
		{
			OnPluginMessageReceivedEvent.Register(OnPluginMessageReceived, Priority.System_Level);
			ModelInfo.Models.Add(new ModelInfo("mario64", 10,10,10, 15));
		}
		
		public override void Unload(bool shutdown)
		{
			OnPluginMessageReceivedEvent.Unregister(OnPluginMessageReceived);
			foreach (ModelInfo m in ModelInfo.Models)
			{
				if (m.Model.CaselessEq("mario64"))
				{
					ModelInfo.Models.Remove(m);
					break;
				}
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

				case MarioOpcodes.OPCODE_MARIO_FORCE:
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
					break;
			}
		}
	}
}
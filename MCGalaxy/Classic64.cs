using System;
using MCGalaxy.Events;
using MCGalaxy.Events.ServerEvents;
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
        }
        
        public override void Unload(bool shutdown)
		{
            OnPluginMessageReceivedEvent.Unregister(OnPluginMessageReceived);
        }

		static void OnPluginMessageReceived(Player p, byte channel, byte[] data)
		{
			if (channel != 64) return;

			switch((MarioOpcodes)data[0])
			{
				case MarioOpcodes.OPCODE_MARIO_HAS_PLUGIN:
					p.Send(Packet.PluginMessage(channel, data));
					break;
			}
		}
	}
}
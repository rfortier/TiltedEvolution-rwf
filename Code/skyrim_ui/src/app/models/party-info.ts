export class PartyInfo {
  playerIds: Array<number>;
  leaderId: number;
  isAutoJoinDisabled: boolean;

  constructor(options: { playerIds: Array<number>; leaderId: number; isAutoJoinDisabled?: boolean }) {
    this.playerIds = options.playerIds;
    this.leaderId = options.leaderId;
    this.isAutoJoinDisabled = options.isAutoJoinDisabled || false;
  }
}

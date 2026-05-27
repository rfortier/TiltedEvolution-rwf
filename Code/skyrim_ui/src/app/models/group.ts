export class Group {
  isEnabled: boolean;
  owner: number;
  members: Array<number>;
  isAutoJoinDisabled: boolean;

  constructor(
    options: {
      isEnabled?: boolean;
      owner?: number;
      members?: Array<number>;
      isAutoJoinDisabled?: boolean;
    } = {},
  ) {
    this.isEnabled = options.isEnabled || false;
    this.owner = options.owner;
    this.members = options.members || new Array<number>();
    this.isAutoJoinDisabled = options.isAutoJoinDisabled || false;
  }
}

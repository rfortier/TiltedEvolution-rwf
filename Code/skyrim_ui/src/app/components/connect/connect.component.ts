import {
  AfterViewInit,
  Component,
  ElementRef,
  EventEmitter,
  HostListener,
  OnDestroy,
  Output,
  ViewChild,
} from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { firstValueFrom, Subscription } from 'rxjs';
import { View } from '../../models/view.enum';
import { AlertService } from '../../services/alert.service';
import { ClientService } from '../../services/client.service';
import { ErrorService } from '../../services/error.service';
import { Sound, SoundService } from '../../services/sound.service';
import { StoreService } from '../../services/store.service';
import { UiRepository } from '../../store/ui.repository';

@Component({
  selector: 'app-connect',
  templateUrl: './connect.component.html',
  styleUrls: ['./connect.component.scss'],
})
export class ConnectComponent implements OnDestroy, AfterViewInit {
  public address = '';
  public password = '';
  public savePassword = false;
  public hidePassword = true;

  public connecting = false;

  @Output() public done = new EventEmitter<void>();

  public constructor(
    private readonly client: ClientService,
    private readonly sound: SoundService,
    private readonly alert: AlertService,
    private readonly errorService: ErrorService,
    private readonly storeService: StoreService,
    private readonly translocoService: TranslocoService,
    private readonly uiRepository: UiRepository,
  ) {
    this.connectionSubscription = this.client.connectionStateChange.subscribe(
      async state => {
        if (this.connecting) {
          this.connecting = false;

          if (state) {
            // The panel used to just close here, with a sound as the only sign
            // anything had happened. Closing is the right thing once the player
            // has acknowledged the result, not before, so the attempt now ends
            // on an explicit "connected" dialog and the panel closes behind it.
            this.alert.show({
              kind: 'success',
              title: await firstValueFrom(
                this.translocoService.selectTranslate<string>(
                  'COMPONENT.CONNECT.SUCCESS.TITLE',
                ),
              ),
              message: await firstValueFrom(
                this.translocoService.selectTranslate<string>(
                  'COMPONENT.CONNECT.SUCCESS.MESSAGE',
                  { address: this.address.trim() },
                ),
              ),
            });
            this.done.next();
          } else if (this.errorService.getError() === '') {
            // show connection error when there is no more specific error
            const message = await firstValueFrom(
              this.translocoService.selectTranslate<string>(
                'COMPONENT.CONNECT.ERROR.CONNECTION',
              ),
            );
            await this.errorService.setError(message);
          }
        }
      },
    );

    // connectionStateChange only goes false when the transport reports a
    // disconnect. An attempt that fails before it ever reaches the server --
    // an unresolvable address, or the client-side handshake deadline -- raises
    // an error instead and never touches that stream, which used to leave this
    // panel showing "connecting" forever. isConnectionInProgressChange is the
    // stream that ends for every outcome, so it is what actually clears the
    // local flag.
    this.inProgressSubscription = this.client.isConnectionInProgressChange.subscribe(
      inProgress => {
        if (!inProgress && this.connecting) {
          this.connecting = false;
        }
      },
    );

    this.protocolMismatchSubscription =
      this.client.protocolMismatchChange.subscribe(async state => {
        if (state) {
          this.connecting = false;
          const message = await firstValueFrom(
            this.translocoService.selectTranslate<string>(
              'COMPONENT.CONNECT.ERROR.VERSION_MISMATCH',
            ),
          );
          await this.errorService.setError(message);
        }
      });

    this.address = this.storeService.get('last_connected_address', '');
    this.password = this.storeService.get('last_connected_password', '');
    this.savePassword = this.password !== '';
  }

  public ngAfterViewInit(): void {
    setTimeout(() => {
      this.inputRef.nativeElement.focus();
    }, 100);
  }

  public ngOnDestroy(): void {
    this.connectionSubscription.unsubscribe();
    this.protocolMismatchSubscription.unsubscribe();
    this.inProgressSubscription.unsubscribe();
  }

  public connect(): void {
    void this.attemptConnect();
  }

  private async attemptConnect(): Promise<void> {
    const address = this.address.trim().match(/^(.+?)(?::([0-9]+))?$/);

    if (!address) {
      this.sound.play(Sound.Fail);
      const message = await firstValueFrom(
        this.translocoService.selectTranslate(
          'COMPONENT.CONNECT.ERROR.INVALID_ADDRESS',
        ),
      );
      await this.errorService.setError(message);
      return;
    }

    this.connecting = true;

    this.storeService.set('last_connected_address', this.address);
    if (this.savePassword) {
      this.storeService.set('last_connected_password', this.password);
    } else {
      this.storeService.remove('last_connected_password');
    }

    this.sound.play(Sound.Ok);
    this.client.connect(
      address[1],
      address[2] ? Number.parseInt(address[2]) : 10578,
      this.password,
    );
  }

  public cancel(): void {
    // Cancelling while a connection is in flight has to abort it, otherwise
    // the attempt keeps running behind a closed popup and the next one stacks
    // on top of it. The client clears its state on the resulting disconnect
    // event, which is what ends the "connecting" state.
    if (this.connecting) {
      this.connecting = false;
      this.client.disconnect();
    }

    this.sound.play(Sound.Cancel);
    this.done.next();
  }

  public openServerList(): void {
    this.uiRepository.openView(View.SERVER_LIST);
  }

  @ViewChild('input')
  private inputRef!: ElementRef;

  private connectionSubscription: Subscription;

  private protocolMismatchSubscription: Subscription;

  private inProgressSubscription: Subscription;

  @HostListener('window:keydown.escape', ['$event'])
  // @ts-ignore
  private activate(event: KeyboardEvent): void {
    if (this.errorService.getError()) {
      this.errorService.removeError();
    } else {
      this.done.next();
    }

    event.stopPropagation();
    event.preventDefault();
  }
}
